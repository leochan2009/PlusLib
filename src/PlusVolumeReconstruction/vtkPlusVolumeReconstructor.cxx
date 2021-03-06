/*=Plus=header=begin======================================================
  Program: Plus
  Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
  See License.txt for details.
=========================================================Plus=header=end*/

// Local includes
#include "PlusConfigure.h"
#include "PlusTrackedFrame.h"
#include "vtkPlusFanAngleDetectorAlgo.h"
#include "vtkPlusFillHolesInVolume.h"
#include "vtkPlusTrackedFrameList.h"
#include "vtkPlusTransformRepository.h"
#include "vtkPlusVolumeReconstructor.h"

// STL includes
#include <limits>

// VTK includes
#include <vtkDataSetWriter.h>
#include <vtkImageData.h>
#include <vtkImageExtractComponents.h>
#include <vtkImageImport.h>
#include <vtkImageViewer.h>
#include <vtkObjectFactory.h>
#include <vtkSmartPointer.h>
#include <vtkTransform.h>
#include <vtkXMLUtilities.h>
#include <vtkPNGReader.h>
#include <vtkImageFlip.h>
#include <vtkImageImport.h>

// ITK includes
#include <metaImage.h>
#include <itkImageFileReader.h>

vtkStandardNewMacro(vtkPlusVolumeReconstructor);

//----------------------------------------------------------------------------
vtkPlusVolumeReconstructor::vtkPlusVolumeReconstructor()
  : ReconstructedVolume(vtkSmartPointer<vtkImageData>::New())
  , Reconstructor(vtkPlusPasteSliceIntoVolume::New())
  , HoleFiller(vtkPlusFillHolesInVolume::New())
  , FanAngleDetector(vtkPlusFanAngleDetectorAlgo::New())
  , FillHoles(false)
  , EnableFanAnglesAutoDetect(false)
  , SkipInterval(1)
  , ReconstructedVolumeUpdatedTime(0)
{
  this->FanAnglesDeg[0] = 0.0;
  this->FanAnglesDeg[1] = 0.0;
}

//----------------------------------------------------------------------------
vtkPlusVolumeReconstructor::~vtkPlusVolumeReconstructor()
{
  if (this->Reconstructor)
  {
    this->Reconstructor->Delete();
    this->Reconstructor = NULL;
  }
  if (this->HoleFiller)
  {
    this->HoleFiller->Delete();
    this->HoleFiller = NULL;
  }
  if (this->FanAngleDetector)
  {
    this->FanAngleDetector->Delete();
    this->FanAngleDetector = NULL;
  }
}

//----------------------------------------------------------------------------
void vtkPlusVolumeReconstructor::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusVolumeReconstructor::ReadConfiguration(vtkXMLDataElement* config)
{
  XML_FIND_NESTED_ELEMENT_REQUIRED(reconConfig, config, "VolumeReconstruction");

  XML_READ_STRING_ATTRIBUTE_OPTIONAL(ReferenceCoordinateFrame, reconConfig);
  XML_READ_STRING_ATTRIBUTE_OPTIONAL(ImageCoordinateFrame, reconConfig);

  XML_READ_VECTOR_ATTRIBUTE_REQUIRED(double, 3, OutputSpacing, reconConfig);
  XML_READ_VECTOR_ATTRIBUTE_OPTIONAL(double, 3, OutputOrigin, reconConfig);
  XML_READ_VECTOR_ATTRIBUTE_OPTIONAL(int, 6, OutputExtent, reconConfig);

  XML_READ_VECTOR_ATTRIBUTE_OPTIONAL(int, 2, ClipRectangleOrigin, reconConfig);
  XML_READ_VECTOR_ATTRIBUTE_OPTIONAL(int, 2, ClipRectangleSize, reconConfig);

  XML_READ_VECTOR_ATTRIBUTE_OPTIONAL(double, 2, FanAngles, reconConfig);  // DEPRECATED (replaced by FanAnglesDeg)
  XML_READ_VECTOR_ATTRIBUTE_OPTIONAL(double, 2, FanAnglesDeg, reconConfig);

  XML_READ_VECTOR_ATTRIBUTE_OPTIONAL(double, 2, FanOrigin, reconConfig);  // DEPRECATED (replaced by FanOriginPixel)
  XML_READ_VECTOR_ATTRIBUTE_OPTIONAL(double, 2, FanOriginPixel, reconConfig);

  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(double, FanDepth, reconConfig);   // DEPRECATED (replaced by FanRadiusStopPixel)
  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(double, FanRadiusStartPixel, reconConfig);
  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(double, FanRadiusStopPixel, reconConfig);

  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(double, FanAnglesAutoDetectBrightnessThreshold, reconConfig);
  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(int, FanAnglesAutoDetectFilterRadiusPixel, reconConfig);

  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(double, PixelRejectionThreshold, reconConfig);

  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(int, SkipInterval, reconConfig);
  if (this->SkipInterval < 1)
  {
    LOG_WARNING("SkipInterval in the config file must be greater or equal to 1. Resetting to 1");
    SkipInterval = 1;
  }

  // reconstruction options
  XML_READ_ENUM2_ATTRIBUTE_OPTIONAL(Interpolation, reconConfig,
                                    this->Reconstructor->GetInterpolationModeAsString(vtkPlusPasteSliceIntoVolume::LINEAR_INTERPOLATION), vtkPlusPasteSliceIntoVolume::LINEAR_INTERPOLATION,
                                    this->Reconstructor->GetInterpolationModeAsString(vtkPlusPasteSliceIntoVolume::NEAREST_NEIGHBOR_INTERPOLATION), vtkPlusPasteSliceIntoVolume::NEAREST_NEIGHBOR_INTERPOLATION);

  XML_READ_ENUM3_ATTRIBUTE_OPTIONAL(Optimization, reconConfig,
                                    this->Reconstructor->GetOptimizationModeAsString(vtkPlusPasteSliceIntoVolume::FULL_OPTIMIZATION), vtkPlusPasteSliceIntoVolume::FULL_OPTIMIZATION,
                                    this->Reconstructor->GetOptimizationModeAsString(vtkPlusPasteSliceIntoVolume::PARTIAL_OPTIMIZATION), vtkPlusPasteSliceIntoVolume::PARTIAL_OPTIMIZATION,
                                    this->Reconstructor->GetOptimizationModeAsString(vtkPlusPasteSliceIntoVolume::NO_OPTIMIZATION), vtkPlusPasteSliceIntoVolume::NO_OPTIMIZATION);

  XML_READ_ENUM4_ATTRIBUTE_OPTIONAL(CompoundingMode, reconConfig,
                                    this->Reconstructor->GetCompoundingModeAsString(vtkPlusPasteSliceIntoVolume::LATEST_COMPOUNDING_MODE), vtkPlusPasteSliceIntoVolume::LATEST_COMPOUNDING_MODE,
                                    this->Reconstructor->GetCompoundingModeAsString(vtkPlusPasteSliceIntoVolume::MEAN_COMPOUNDING_MODE), vtkPlusPasteSliceIntoVolume::MEAN_COMPOUNDING_MODE,
                                    this->Reconstructor->GetCompoundingModeAsString(vtkPlusPasteSliceIntoVolume::IMPORTANCE_MASK_COMPOUNDING_MODE), vtkPlusPasteSliceIntoVolume::IMPORTANCE_MASK_COMPOUNDING_MODE,
                                    this->Reconstructor->GetCompoundingModeAsString(vtkPlusPasteSliceIntoVolume::MAXIMUM_COMPOUNDING_MODE), vtkPlusPasteSliceIntoVolume::MAXIMUM_COMPOUNDING_MODE);

  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(int, NumberOfThreads, reconConfig);

  if (this->Reconstructor->GetCompoundingMode() == vtkPlusPasteSliceIntoVolume::IMPORTANCE_MASK_COMPOUNDING_MODE)
  {
    XML_READ_CSTRING_ATTRIBUTE_REQUIRED(ImportanceMaskFilename, reconConfig);
  }

  XML_READ_ENUM2_ATTRIBUTE_OPTIONAL(FillHoles, reconConfig, "ON", true, "OFF", false);
  XML_READ_BOOL_ATTRIBUTE_OPTIONAL(EnableFanAnglesAutoDetect, reconConfig);

  // Find and read kernels. First for loop counts the number of kernels to allocate, second for loop stores them
  if (this->FillHoles)
  {
    // load input for kernel size, stdev, etc...
    XML_FIND_NESTED_ELEMENT_REQUIRED(holeFilling, reconConfig, "HoleFilling");
    if (this->HoleFiller->ReadConfiguration(holeFilling) != PLUS_SUCCESS)
    {
      return PLUS_FAIL;
    }
  }

  // ==== Warn if using DEPRECATED XML tags (2014-08-15, #923) ====
  XML_READ_WARNING_DEPRECATED_CSTRING_REPLACED(Compounding, reconConfig, CompoundingMode);
  XML_READ_ENUM2_ATTRIBUTE_OPTIONAL(Compounding, reconConfig, "ON", 1, "OFF", 0);

  XML_READ_WARNING_DEPRECATED_CSTRING_REPLACED(Calculation, reconConfig, CompoundingMode);
  XML_READ_ENUM2_ATTRIBUTE_OPTIONAL(Calculation, reconConfig,
                                    this->Reconstructor->GetCalculationAsString(vtkPlusPasteSliceIntoVolume::WEIGHTED_AVERAGE_CALCULATION), vtkPlusPasteSliceIntoVolume::WEIGHTED_AVERAGE_CALCULATION,
                                    this->Reconstructor->GetCalculationAsString(vtkPlusPasteSliceIntoVolume::MAXIMUM_CALCULATION), vtkPlusPasteSliceIntoVolume::MAXIMUM_CALCULATION);

  if (this->Reconstructor->GetCompoundingMode() == vtkPlusPasteSliceIntoVolume::UNDEFINED_COMPOUNDING_MODE)
  {
    if (this->Reconstructor->GetCalculation() == vtkPlusPasteSliceIntoVolume::MAXIMUM_CALCULATION)
    {
      SetCompoundingMode(vtkPlusPasteSliceIntoVolume::MAXIMUM_COMPOUNDING_MODE);
    }
    else if (this->Reconstructor->GetCompounding() == 0)
    {
      SetCompoundingMode(vtkPlusPasteSliceIntoVolume::LATEST_COMPOUNDING_MODE);
    }
    else
    {
      SetCompoundingMode(vtkPlusPasteSliceIntoVolume::MEAN_COMPOUNDING_MODE);
    }
    LOG_WARNING("CompoundingMode has not been set. Will assume " << this->Reconstructor->GetCompoundingModeAsString(this->Reconstructor->GetCompoundingMode()) << ".");
  }

  this->Modified();

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
// Get the XML element describing the freehand object
PlusStatus vtkPlusVolumeReconstructor::WriteConfiguration(vtkXMLDataElement* config)
{
  XML_FIND_NESTED_ELEMENT_CREATE_IF_MISSING(reconConfig, config, "VolumeReconstruction");

  XML_WRITE_STRING_ATTRIBUTE_IF_NOT_EMPTY(ImageCoordinateFrame, config);
  XML_WRITE_STRING_ATTRIBUTE_IF_NOT_EMPTY(ReferenceCoordinateFrame, config);

  // output parameters
  reconConfig->SetVectorAttribute("OutputSpacing", 3, this->Reconstructor->GetOutputSpacing());
  reconConfig->SetVectorAttribute("OutputOrigin", 3, this->Reconstructor->GetOutputOrigin());
  reconConfig->SetVectorAttribute("OutputExtent", 6, this->Reconstructor->GetOutputExtent());

  // clipping parameters
  reconConfig->SetVectorAttribute("ClipRectangleOrigin", 2, this->Reconstructor->GetClipRectangleOrigin());
  reconConfig->SetVectorAttribute("ClipRectangleSize", 2, this->Reconstructor->GetClipRectangleSize());

  // Fan parameters
  // remove deprecated attributes
  XML_REMOVE_ATTRIBUTE(reconConfig, "FanDepth");
  XML_REMOVE_ATTRIBUTE(reconConfig, "FanOrigin");
  XML_REMOVE_ATTRIBUTE(reconConfig, "FanAngles");
  if (this->Reconstructor->FanClippingApplied())
  {
    reconConfig->SetVectorAttribute("FanAnglesDeg", 2, this->Reconstructor->GetFanAnglesDeg());
    // Image spacing is 1.0, so reconstructor's fan origin and radius values are in pixels
    reconConfig->SetVectorAttribute("FanOriginPixel", 2, this->Reconstructor->GetFanOrigin());
    reconConfig->SetDoubleAttribute("FanRadiusStartPixel", this->Reconstructor->GetFanRadiusStart());
    reconConfig->SetDoubleAttribute("FanRadiusStopPixel", this->Reconstructor->GetFanRadiusStop());
    XML_WRITE_BOOL_ATTRIBUTE(EnableFanAnglesAutoDetect, reconConfig);
    if (this->EnableFanAnglesAutoDetect)
    {
      reconConfig->SetDoubleAttribute("FanAnglesAutoDetectBrightnessThreshold", this->FanAngleDetector->GetBrightnessThreshold());
      reconConfig->SetIntAttribute("FanAnglesAutoDetectFilterRadiusPixel", this->FanAngleDetector->GetFilterRadiusPixel());
    }
  }
  else
  {
    XML_REMOVE_ATTRIBUTE(reconConfig, "FanAnglesDeg");
    XML_REMOVE_ATTRIBUTE(reconConfig, "EnableFanAnglesAutoDetect")
    XML_REMOVE_ATTRIBUTE(reconConfig, "FanOriginPixel");
    XML_REMOVE_ATTRIBUTE(reconConfig, "FanRadiusStartPixel");
    XML_REMOVE_ATTRIBUTE(reconConfig, "FanRadiusStopPixel");
  }

  // reconstruction options
  reconConfig->SetAttribute("Interpolation", this->Reconstructor->GetInterpolationModeAsString(this->Reconstructor->GetInterpolationMode()));
  reconConfig->SetAttribute("Optimization", this->Reconstructor->GetOptimizationModeAsString(this->Reconstructor->GetOptimization()));
  reconConfig->SetAttribute("CompoundingMode", this->Reconstructor->GetCompoundingModeAsString(this->Reconstructor->GetCompoundingMode()));

  if (this->Reconstructor->GetNumberOfThreads() > 0)
  {
    reconConfig->SetIntAttribute("NumberOfThreads", this->Reconstructor->GetNumberOfThreads());
  }
  else
  {
    XML_REMOVE_ATTRIBUTE(reconConfig, "NumberOfThreads");
  }

  if (this->Reconstructor->GetCompoundingMode() == vtkPlusPasteSliceIntoVolume::IMPORTANCE_MASK_COMPOUNDING_MODE)
  {
    reconConfig->SetAttribute("ImportanceMaskFilename", this->ImportanceMaskFilename.c_str());
  }
  else
  {
    XML_REMOVE_ATTRIBUTE(reconConfig, "ImportanceMaskFilename");
  }

  if (this->Reconstructor->IsPixelRejectionEnabled())
  {
    reconConfig->SetDoubleAttribute("PixelRejectionThreshold", this->GetPixelRejectionThreshold());
  }
  else
  {
    XML_REMOVE_ATTRIBUTE(reconConfig, "PixelRejectionThreshold");
  }

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
void vtkPlusVolumeReconstructor::AddImageToExtent(vtkImageData* image, vtkMatrix4x4* imageToReference, double* extent_Ref)
{
  // Output volume is in the Reference coordinate system.

  // Prepare the four corner points of the input US image.
  int* frameExtent = image->GetExtent();
  std::vector< double* > corners_ImagePix;
  double minX = frameExtent[0];
  double maxX = frameExtent[1];
  double minY = frameExtent[2];
  double maxY = frameExtent[3];
  if (this->Reconstructor->GetClipRectangleSize()[0] > 0 && this->Reconstructor->GetClipRectangleSize()[1] > 0)
  {
    // Clipping rectangle is specified
    minX = std::max<double>(minX, this->Reconstructor->GetClipRectangleOrigin()[0]);
    maxX = std::min<double>(maxX, this->Reconstructor->GetClipRectangleOrigin()[0] + this->Reconstructor->GetClipRectangleSize()[0]);
    minY = std::max<double>(minY, this->Reconstructor->GetClipRectangleOrigin()[1]);
    maxY = std::min<double>(maxY, this->Reconstructor->GetClipRectangleOrigin()[1] + this->Reconstructor->GetClipRectangleSize()[1]);
  }
  double c0[ 4 ] = { minX, minY, 0,  1 };
  double c1[ 4 ] = { minX, maxY, 0,  1 };
  double c2[ 4 ] = { maxX, minY, 0,  1 };
  double c3[ 4 ] = { maxX, maxY, 0,  1 };
  corners_ImagePix.push_back(c0);
  corners_ImagePix.push_back(c1);
  corners_ImagePix.push_back(c2);
  corners_ImagePix.push_back(c3);

  // Transform the corners to Reference and expand the extent if needed
  for (unsigned int corner = 0; corner < corners_ImagePix.size(); ++corner)
  {
    double corner_Ref[ 4 ] = { 0, 0, 0, 1 }; // position of the corner in the Reference coordinate system
    imageToReference->MultiplyPoint(corners_ImagePix[corner], corner_Ref);

    for (int axis = 0; axis < 3; axis ++)
    {
      if (corner_Ref[axis] < extent_Ref[axis * 2])
      {
        // min extent along this coord axis has to be decreased
        extent_Ref[axis * 2] = corner_Ref[axis];
      }
      if (corner_Ref[axis] > extent_Ref[axis * 2 + 1])
      {
        // max extent along this coord axis has to be increased
        extent_Ref[axis * 2 + 1] = corner_Ref[axis];
      }
    }
  }
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusVolumeReconstructor::GetImageToReferenceTransformName(PlusTransformName& imageToReferenceTransformName)
{
  // image to reference transform is specified in the XML tree
  imageToReferenceTransformName = PlusTransformName(this->ImageCoordinateFrame, this->ReferenceCoordinateFrame);
  if (!imageToReferenceTransformName.IsValid())
  {
    LOG_ERROR("Failed to set ImageToReference transform name from '" << this->ImageCoordinateFrame << "' to '" << this->ReferenceCoordinateFrame << "'");
    return PLUS_FAIL;
  }
  return PLUS_SUCCESS;

  if (this->ImageCoordinateFrame.empty())
  {
    LOG_ERROR("Image coordinate frame name is undefined");
  }
  if (this->ReferenceCoordinateFrame.empty())
  {
    LOG_ERROR("Reference coordinate frame name is undefined");
  }
  return PLUS_FAIL;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusVolumeReconstructor::SetOutputExtentFromFrameList(vtkPlusTrackedFrameList* trackedFrameList, vtkPlusTransformRepository* transformRepository, std::string& errorDescription)
{
  PlusTransformName imageToReferenceTransformName;
  if (GetImageToReferenceTransformName(imageToReferenceTransformName) != PLUS_SUCCESS)
  {
    errorDescription = "Invalid ImageToReference transform name";
    LOG_ERROR(errorDescription);
    return PLUS_FAIL;
  }

  if (trackedFrameList == NULL)
  {
    errorDescription = "No valid frames are available";
    LOG_ERROR("Failed to set output extent from tracked frame list - input frame list is NULL");
    return PLUS_FAIL;
  }
  if (trackedFrameList->GetNumberOfTrackedFrames() == 0)
  {
    errorDescription = "No valid frames are available";
    LOG_ERROR("Failed to set output extent from tracked frame list - input frame list is empty");
    return PLUS_FAIL;
  }

  if (transformRepository == NULL)
  {
    errorDescription = "Error in transform repository";
    LOG_ERROR("Failed to set output extent from tracked frame list - input transform repository is NULL");
    return PLUS_FAIL;
  }

  double extent_Ref[6] =
  {
    VTK_DOUBLE_MAX, VTK_DOUBLE_MIN,
    VTK_DOUBLE_MAX, VTK_DOUBLE_MIN,
    VTK_DOUBLE_MAX, VTK_DOUBLE_MIN
  };

  const int numberOfFrames = trackedFrameList->GetNumberOfTrackedFrames();
  int numberOfValidFrames = 0;
  for (int frameIndex = 0; frameIndex < numberOfFrames; ++frameIndex)
  {
    PlusTrackedFrame* frame = trackedFrameList->GetTrackedFrame(frameIndex);

    if (transformRepository->SetTransforms(*frame) != PLUS_SUCCESS)
    {
      LOG_ERROR("Failed to update transform repository with tracked frame!");
      continue;
    }

    // Get transform
    bool isMatrixValid(false);
    vtkSmartPointer<vtkMatrix4x4> imageToReferenceTransformMatrix = vtkSmartPointer<vtkMatrix4x4>::New();
    if (transformRepository->GetTransform(imageToReferenceTransformName, imageToReferenceTransformMatrix, &isMatrixValid) != PLUS_SUCCESS)
    {
      std::string strImageToReferenceTransformName;
      imageToReferenceTransformName.GetTransformName(strImageToReferenceTransformName);
      LOG_WARNING("Failed to get transform '" << strImageToReferenceTransformName << "' from transform repository!");
      continue;
    }

    if (isMatrixValid)
    {
      numberOfValidFrames++;

      // Get image (only the frame extents will be used)
      vtkImageData* frameImage = trackedFrameList->GetTrackedFrame(frameIndex)->GetImageData()->GetImage();

      // Expand the extent_Ref to include this frame
      AddImageToExtent(frameImage, imageToReferenceTransformMatrix, extent_Ref);
    }
  }

  LOG_DEBUG("Automatic volume extent computation from frames used " << numberOfValidFrames << " out of " << numberOfFrames << " (probably wrong image or reference coordinate system was defined or all transforms were invalid)");
  if (numberOfValidFrames == 0)
  {
    std::string strImageToReferenceTransformName;
    imageToReferenceTransformName.GetTransformName(strImageToReferenceTransformName);
    errorDescription = "Automatic volume extent computation failed, there were no valid " + strImageToReferenceTransformName + " transform available in the whole sequence";
    LOG_ERROR(errorDescription);
    return PLUS_FAIL;
  }

  // Set the output extent from the current min and max values, using the user-defined image resolution.
  int outputExtent[ 6 ] = { 0, 0, 0, 0, 0, 0 };
  double* outputSpacing = this->Reconstructor->GetOutputSpacing();
  outputExtent[ 1 ] = int(std::ceil((extent_Ref[1] - extent_Ref[0]) / outputSpacing[ 0 ]));
  outputExtent[ 3 ] = int(std::ceil((extent_Ref[3] - extent_Ref[2]) / outputSpacing[ 1 ]));
  outputExtent[ 5 ] = int(std::ceil((extent_Ref[5] - extent_Ref[4]) / outputSpacing[ 2 ]));

  this->Reconstructor->SetOutputScalarMode(trackedFrameList->GetTrackedFrame(0)->GetImageData()->GetImage()->GetScalarType());
  this->Reconstructor->SetOutputExtent(outputExtent);
  this->Reconstructor->SetOutputOrigin(extent_Ref[0], extent_Ref[2], extent_Ref[4]);
  try
  {
    if (this->Reconstructor->ResetOutput() != PLUS_SUCCESS) // :TODO: call this automatically
    {
      errorDescription = "Failed to initialize output volume of the reconstructor";
      LOG_ERROR(errorDescription);
      return PLUS_FAIL;
    }
  }
  catch (std::bad_alloc& e)
  {
    cerr << e.what() << endl;
    errorDescription = "StartReconstruction failed due to out of memory. Try to reduce the size or increase spacing of the output volume.";
    LOG_ERROR(errorDescription);
    return PLUS_FAIL;
  }

  this->Modified();

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusVolumeReconstructor::AddTrackedFrame(PlusTrackedFrame* frame, vtkPlusTransformRepository* transformRepository, bool* insertedIntoVolume/*=NULL*/)
{
  PlusTransformName imageToReferenceTransformName;
  if (GetImageToReferenceTransformName(imageToReferenceTransformName) != PLUS_SUCCESS)
  {
    LOG_ERROR("Invalid ImageToReference transform name");
    return PLUS_FAIL;
  }

  if (frame == NULL)
  {
    LOG_ERROR("Failed to add tracked frame to volume - input frame is NULL");
    return PLUS_FAIL;
  }

  if (transformRepository == NULL)
  {
    LOG_ERROR("Failed to add tracked frame to volume - input transform repository is NULL");
    return PLUS_FAIL;
  }

  bool isMatrixValid(false);
  vtkSmartPointer<vtkMatrix4x4> imageToReferenceTransformMatrix = vtkSmartPointer<vtkMatrix4x4>::New();
  if (transformRepository->GetTransform(imageToReferenceTransformName, imageToReferenceTransformMatrix, &isMatrixValid) != PLUS_SUCCESS)
  {
    std::string strImageToReferenceTransformName;
    imageToReferenceTransformName.GetTransformName(strImageToReferenceTransformName);
    LOG_ERROR("Failed to get transform '" << strImageToReferenceTransformName << "' from transform repository");
    return PLUS_FAIL;
  }

  if (insertedIntoVolume != NULL)
  {
    *insertedIntoVolume = isMatrixValid;
  }

  if (!isMatrixValid)
  {
    // Insert only valid frame into volume
    std::string strImageToReferenceTransformName;
    imageToReferenceTransformName.GetTransformName(strImageToReferenceTransformName);
    LOG_DEBUG("Transform '" << strImageToReferenceTransformName << "' is invalid for the current frame, therefore this frame is not be inserted into the volume");
    return PLUS_SUCCESS;
  }

  vtkImageData* frameImage = frame->GetImageData()->GetImage();
  bool isImageEmpty = false;
  UpdateFanAnglesFromImage(frameImage, isImageEmpty);
  if (isImageEmpty)
  {
    // nothing to insert, image is empty
    return PLUS_SUCCESS;
  }

  PlusStatus status = this->Reconstructor->InsertSlice(frameImage, imageToReferenceTransformMatrix);
  this->Modified();
  return status;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusVolumeReconstructor::UpdateReconstructedVolume()
{
  // Load reconstructed volume if the algorithm configuration was modified since the last load
  //   MTime is updated whenever a new frame is added or configuration modified
  //   ReconstructedVolumeUpdatedTime is updated whenever a reconstruction was completed
  if (this->ReconstructedVolumeUpdatedTime >= this->GetMTime())
  {
    // reconstruction is already up-to-date
    return PLUS_SUCCESS;
  }

  if (this->FillHoles)
  {
    if (this->GenerateHoleFilledVolume() != PLUS_SUCCESS)
    {
      LOG_ERROR("Failed to generate hole filled volume!");
      return PLUS_FAIL;
    }
  }
  else
  {
    this->ReconstructedVolume->DeepCopy(this->Reconstructor->GetReconstructedVolume());
  }

  this->ReconstructedVolumeUpdatedTime = this->GetMTime();

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusVolumeReconstructor::GetReconstructedVolume(vtkImageData* volume)
{
  if (this->UpdateReconstructedVolume() != PLUS_SUCCESS)
  {
    LOG_ERROR("Failed to load reconstructed volume");
    return PLUS_FAIL;
  }

  volume->DeepCopy(this->ReconstructedVolume);

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusVolumeReconstructor::GenerateHoleFilledVolume()
{
  LOG_INFO("Hole Filling has begun");
  this->HoleFiller->SetReconstructedVolume(this->Reconstructor->GetReconstructedVolume());
  this->HoleFiller->SetAccumulationBuffer(this->Reconstructor->GetAccumulationBuffer());
  this->HoleFiller->Update();
  LOG_INFO("Hole Filling has finished");

  this->ReconstructedVolume->DeepCopy(HoleFiller->GetOutput());

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusVolumeReconstructor::ExtractGrayLevels(vtkImageData* reconstructedVolume)
{
  if (this->UpdateReconstructedVolume() != PLUS_SUCCESS)
  {
    LOG_ERROR("Failed to load reconstructed volume");
    return PLUS_FAIL;
  }

  vtkSmartPointer<vtkImageExtractComponents> extract = vtkSmartPointer<vtkImageExtractComponents>::New();

  extract->SetComponents(0);
  extract->SetInputData(this->ReconstructedVolume);
  extract->Update();

  reconstructedVolume->DeepCopy(extract->GetOutput());

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusVolumeReconstructor::ExtractAccumulation(vtkImageData* accumulationBuffer)
{
  if (this->UpdateReconstructedVolume() != PLUS_SUCCESS)
  {
    LOG_ERROR("Failed to load reconstructed volume");
    return PLUS_FAIL;
  }

  vtkSmartPointer<vtkImageExtractComponents> extract = vtkSmartPointer<vtkImageExtractComponents>::New();

  extract->SetComponents(0);
  extract->SetInputData(this->Reconstructor->GetAccumulationBuffer());
  extract->Update();

  accumulationBuffer->DeepCopy(extract->GetOutput());

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusVolumeReconstructor::SaveReconstructedVolumeToMetafile(const std::string& filename, bool accumulation/*=false*/, bool useCompression/*=true*/)
{
  vtkSmartPointer<vtkImageData> volumeToSave = vtkSmartPointer<vtkImageData>::New();
  if (accumulation)
  {
    if (this->ExtractAccumulation(volumeToSave) != PLUS_SUCCESS)
    {
      LOG_ERROR("Extracting accumulation buffer failed!");
      return PLUS_FAIL;
    }
  }
  else
  {
    if (this->ExtractGrayLevels(volumeToSave) != PLUS_SUCCESS)
    {
      LOG_ERROR("Extracting gray channel failed!");
      return PLUS_FAIL;
    }
  }
  return SaveReconstructedVolumeToMetafile(volumeToSave, filename, useCompression);
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusVolumeReconstructor::SaveReconstructedVolumeToMetafile(vtkImageData* volumeToSave, const std::string& filename, bool useCompression/*=true*/)
{
  if (volumeToSave == NULL)
  {
    LOG_ERROR("vtkPlusVolumeReconstructor::SaveReconstructedVolumeToMetafile: invalid input image");
    return PLUS_FAIL;
  }

  MET_ValueEnumType scalarType = MET_NONE;
  switch (volumeToSave->GetScalarType())
  {
    case VTK_UNSIGNED_CHAR:
      scalarType = MET_UCHAR;
      break;
    case VTK_UNSIGNED_SHORT:
      scalarType = MET_USHORT;
      break;
    case VTK_FLOAT:
      scalarType = MET_FLOAT;
      break;
    default:
      LOG_ERROR("Scalar type is not supported!");
      return PLUS_FAIL;
  }

  MetaImage metaImage(volumeToSave->GetDimensions()[0], volumeToSave->GetDimensions()[1], volumeToSave->GetDimensions()[2],
                      volumeToSave->GetSpacing()[0], volumeToSave->GetSpacing()[1], volumeToSave->GetSpacing()[2],
                      scalarType, 1, volumeToSave->GetScalarPointer());
  metaImage.Origin(volumeToSave->GetOrigin());
  // By definition, LPS orientation in DICOM sense = RAI orientation in MetaIO. See details at:
  // http://www.itk.org/Wiki/Proposals:Orientation#Some_notes_on_the_DICOM_convention_and_current_ITK_usage
  metaImage.AnatomicalOrientation("RAI");
  metaImage.BinaryData(true);
  metaImage.CompressedData(useCompression);
  metaImage.ElementDataFileName("LOCAL");
  if (metaImage.Write(filename.c_str()) == false)
  {
    LOG_ERROR("Failed to save reconstructed volume in sequence metafile!");
    return PLUS_FAIL;
  }

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
int* vtkPlusVolumeReconstructor::GetClipRectangleOrigin()
{
  return this->Reconstructor->GetClipRectangleOrigin();
}

//----------------------------------------------------------------------------
int* vtkPlusVolumeReconstructor::GetClipRectangleSize()
{
  return this->Reconstructor->GetClipRectangleSize();
}

//----------------------------------------------------------------------------
void vtkPlusVolumeReconstructor::Reset()
{
  this->Reconstructor->ResetOutput();
}

//----------------------------------------------------------------------------
void vtkPlusVolumeReconstructor::UpdateFanAnglesFromImage(vtkImageData* frameImage, bool& isImageEmpty)
{
  isImageEmpty = false;
  if (this->EnableFanAnglesAutoDetect)
  {
    this->FanAngleDetector->SetMaxFanAnglesDeg(this->FanAnglesDeg);
    this->FanAngleDetector->SetImage(frameImage);
    this->FanAngleDetector->Update();
    if (this->FanAngleDetector->GetIsFrameEmpty())
    {
      // no image content is found
      LOG_TRACE("No image data is detected in the current frame, the frame is skipped");
      this->Reconstructor->SetFanAnglesDeg(this->FanAnglesDeg); // to make sure fan enable/disable is computed correctly
      isImageEmpty = true;
      return;
    }
    this->Reconstructor->SetFanAnglesDeg(this->FanAngleDetector->GetDetectedFanAnglesDeg());
  }
  else
  {
    this->Reconstructor->SetFanAnglesDeg(this->FanAnglesDeg);
  }
}

//----------------------------------------------------------------------------
void vtkPlusVolumeReconstructor::SetOutputOrigin(double* origin)
{
  this->Reconstructor->SetOutputOrigin(origin);
}

//----------------------------------------------------------------------------
void vtkPlusVolumeReconstructor::SetOutputSpacing(double* spacing)
{
  this->Reconstructor->SetOutputSpacing(spacing);
}

//----------------------------------------------------------------------------
void vtkPlusVolumeReconstructor::SetOutputExtent(int* extent)
{
  this->Reconstructor->SetOutputExtent(extent);
}

//----------------------------------------------------------------------------
void vtkPlusVolumeReconstructor::SetNumberOfThreads(int numberOfThreads)
{
  this->Reconstructor->SetNumberOfThreads(numberOfThreads);
  this->HoleFiller->SetNumberOfThreads(numberOfThreads);
}

//----------------------------------------------------------------------------
void vtkPlusVolumeReconstructor::SetClipRectangleOrigin(int* origin)
{
  this->Reconstructor->SetClipRectangleOrigin(origin);
  this->FanAngleDetector->SetClipRectangleOrigin(origin);
}

//----------------------------------------------------------------------------
void vtkPlusVolumeReconstructor::SetClipRectangleSize(int* size)
{
  this->Reconstructor->SetClipRectangleSize(size);
  this->FanAngleDetector->SetClipRectangleSize(size);
}

//----------------------------------------------------------------------------
void vtkPlusVolumeReconstructor::SetFanAnglesDeg(double* anglesDeg)
{
  this->FanAnglesDeg[0] = anglesDeg[0];
  this->FanAnglesDeg[1] = anglesDeg[1];
}

//----------------------------------------------------------------------------
void vtkPlusVolumeReconstructor::SetFanAngles(double* anglesDeg)
{
  LOG_WARNING("FanAngles volume reconstructor parameter is deprecated. Use FanAnglesDeg instead (with the same value).");
  this->Reconstructor->SetFanAnglesDeg(anglesDeg);
}

//----------------------------------------------------------------------------
void vtkPlusVolumeReconstructor::SetFanOriginPixel(double* originPixel)
{
  // Image coordinate system has unit spacing, so we can set the pixel values directly
  this->Reconstructor->SetFanOrigin(originPixel);
  this->FanAngleDetector->SetFanOrigin(originPixel);
}

//----------------------------------------------------------------------------
void vtkPlusVolumeReconstructor::SetFanOrigin(double* originPixel)
{
  LOG_WARNING("FanOrigin volume reconstructor parameter is deprecated. Use FanOriginPixels instead (with the same value).");
  SetFanOriginPixel(originPixel);
}

//----------------------------------------------------------------------------
void vtkPlusVolumeReconstructor::SetFanRadiusStartPixel(double radiusPixel)
{
  // Image coordinate system has unit spacing, so we can set the pixel values directly
  this->Reconstructor->SetFanRadiusStart(radiusPixel);
  this->FanAngleDetector->SetFanRadiusStart(radiusPixel);
}

//----------------------------------------------------------------------------
void vtkPlusVolumeReconstructor::SetFanRadiusStopPixel(double radiusPixel)
{
  // Image coordinate system has unit spacing, so we can set the pixel values directly
  this->Reconstructor->SetFanRadiusStop(radiusPixel);
  this->FanAngleDetector->SetFanRadiusStop(radiusPixel);
}

//----------------------------------------------------------------------------
void vtkPlusVolumeReconstructor::SetFanDepth(double fanDepthPixel)
{
  LOG_WARNING("FanDepth volume reconstructor parameter is deprecated. Use FanRadiusStopPixels instead (with the same value).");
  SetFanRadiusStopPixel(fanDepthPixel);
}

//----------------------------------------------------------------------------
void vtkPlusVolumeReconstructor::SetInterpolation(vtkPlusPasteSliceIntoVolume::InterpolationType interpolation)
{
  this->Reconstructor->SetInterpolationMode(interpolation);
}

//----------------------------------------------------------------------------
void vtkPlusVolumeReconstructor::SetCompoundingMode(vtkPlusPasteSliceIntoVolume::CompoundingType compoundingMode)
{
  this->Reconstructor->SetCompoundingMode(compoundingMode);
}

//----------------------------------------------------------------------------
void vtkPlusVolumeReconstructor::SetOptimization(vtkPlusPasteSliceIntoVolume::OptimizationType optimization)
{
  this->Reconstructor->SetOptimization(optimization);
}

//----------------------------------------------------------------------------
void vtkPlusVolumeReconstructor::SetCalculation(vtkPlusPasteSliceIntoVolume::CalculationTypeDeprecated type)
{
  this->Reconstructor->SetCalculation(type);
}

//----------------------------------------------------------------------------
void vtkPlusVolumeReconstructor::SetCompounding(int comp)
{
  this->Reconstructor->SetCompounding(comp);
}

//----------------------------------------------------------------------------
void vtkPlusVolumeReconstructor::SetPixelRejectionThreshold(double threshold)
{
  this->Reconstructor->SetPixelRejectionThreshold(threshold);
}

//----------------------------------------------------------------------------
double vtkPlusVolumeReconstructor::GetPixelRejectionThreshold()
{
  return this->Reconstructor->GetPixelRejectionThreshold();
}

//----------------------------------------------------------------------------
bool vtkPlusVolumeReconstructor::FanClippingApplied()
{
  return this->Reconstructor->FanClippingApplied();
}

//----------------------------------------------------------------------------
double* vtkPlusVolumeReconstructor::GetFanOrigin()
{
  return this->Reconstructor->GetFanOrigin();
}

//----------------------------------------------------------------------------
double* vtkPlusVolumeReconstructor::GetFanAnglesDeg()
{
  return this->FanAnglesDeg;
}

//----------------------------------------------------------------------------
double* vtkPlusVolumeReconstructor::GetDetectedFanAnglesDeg()
{
  return this->Reconstructor->GetFanAnglesDeg();
}

//----------------------------------------------------------------------------
double vtkPlusVolumeReconstructor::GetFanRadiusStartPixel()
{
  return this->Reconstructor->GetFanRadiusStart();
}

//----------------------------------------------------------------------------
double vtkPlusVolumeReconstructor::GetFanRadiusStopPixel()
{
  return this->Reconstructor->GetFanRadiusStop();
}

//----------------------------------------------------------------------------
void vtkPlusVolumeReconstructor::SetFanAnglesAutoDetectBrightnessThreshold(double threshold)
{
  this->FanAngleDetector->SetBrightnessThreshold(threshold);
}

//----------------------------------------------------------------------------
void vtkPlusVolumeReconstructor::SetFanAnglesAutoDetectFilterRadiusPixel(int radiusPixel)
{
  this->FanAngleDetector->SetFilterRadiusPixel(radiusPixel);
}

//----------------------------------------------------------------------------
void vtkPlusVolumeReconstructor::SetImportanceMaskFilename(const std::string& filename)
{
  if (this->ImportanceMaskFilename != filename)
  {
    this->ImportanceMaskFilename = filename;

    vtkSmartPointer<vtkPNGReader> reader = vtkSmartPointer<vtkPNGReader>::New();
    reader->SetFileName(filename.c_str());
    reader->Update();

    vtkSmartPointer<vtkImageFlip> flipYFilter = vtkSmartPointer<vtkImageFlip>::New();
    flipYFilter->SetFilteredAxis(1); // flip y axis
    flipYFilter->SetInputConnection(reader->GetOutputPort());
    flipYFilter->Update();
    this->Reconstructor->SetImportanceMask(flipYFilter->GetOutput());

    this->Reconstructor->Modified();
    this->Modified();
  }
}
