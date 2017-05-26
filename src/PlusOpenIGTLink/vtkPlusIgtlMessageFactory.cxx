/*=Plus=header=begin======================================================
Program: Plus
Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
See License.txt for details.
=========================================================Plus=header=end*/

#include "PlusConfigure.h"

#include "PlusTrackedFrame.h"
#include "PlusVideoFrame.h"
#include "vtkImageData.h"
#include "vtkMatrix4x4.h"
#include "vtkObjectFactory.h"
#include "vtkPlusIgtlMessageCommon.h"
#include "vtkPlusIgtlMessageFactory.h"
#include "vtkPlusTrackedFrameList.h"
#include "vtkPlusTransformRepository.h"
#include "vtksys/SystemTools.hxx"
#include <typeinfo>

//----------------------------------------------------------------------------
// IGT message types
#include "igtlCommandMessage.h"
#include "igtlImageMessage.h"
#include "igtlPlusClientInfoMessage.h"
#include "igtlPlusTrackedFrameMessage.h"
#include "igtlPlusUsMessage.h"
#include "igtlPositionMessage.h"
#include "igtlStatusMessage.h"
#include "igtlTrackingDataMessage.h"
#include "igtlTransformMessage.h"

//----------------------------------------------------------------------------

vtkStandardNewMacro(vtkPlusIgtlMessageFactory);

//----------------------------------------------------------------------------
vtkPlusIgtlMessageFactory::vtkPlusIgtlMessageFactory()
  : IgtlFactory(igtl::MessageFactory::New())
{
  this->IgtlFactory->AddMessageType("CLIENTINFO", (PointerToMessageBaseNew)&igtl::PlusClientInfoMessage::New);
  this->IgtlFactory->AddMessageType("TRACKEDFRAME", (PointerToMessageBaseNew)&igtl::PlusTrackedFrameMessage::New);
  this->IgtlFactory->AddMessageType("USMESSAGE", (PointerToMessageBaseNew)&igtl::PlusUsMessage::New);
  configFile = "";
  videoStreamEncoderMap.clear();
}

//----------------------------------------------------------------------------
vtkPlusIgtlMessageFactory::~vtkPlusIgtlMessageFactory()
{
  std::map<std::string, GenericEncoder*>::iterator itr;
  if (this->videoStreamEncoderMap.size())
  {
    while (itr != this->videoStreamEncoderMap.end())
    {
      // found it - delete it
      itr->second->~GenericEncoder();
      this->videoStreamEncoderMap.erase(itr);
      itr++;
    }
  }
}

//----------------------------------------------------------------------------
void vtkPlusIgtlMessageFactory::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  this->PrintAvailableMessageTypes(os, indent);
}

//----------------------------------------------------------------------------
vtkPlusIgtlMessageFactory::PointerToMessageBaseNew vtkPlusIgtlMessageFactory::GetMessageTypeNewPointer(const std::string& messageTypeName)
{
  return this->IgtlFactory->GetMessageTypeNewPointer(messageTypeName);
}

//----------------------------------------------------------------------------
void vtkPlusIgtlMessageFactory::PrintAvailableMessageTypes(ostream& os, vtkIndent indent)
{
  os << indent << "Supported OpenIGTLink message types: " << std::endl;
  std::vector<std::string> types;
  this->IgtlFactory->GetAvailableMessageTypes(types);
  for (std::vector<std::string>::iterator it = types.begin(); it != types.end(); ++it)
  {
    os << indent.GetNextIndent() << "- " << *it << std::endl;
  }
}

//----------------------------------------------------------------------------
igtl::MessageHeader::Pointer vtkPlusIgtlMessageFactory::CreateHeaderMessage(int headerVersion) const
{
  return this->IgtlFactory->CreateHeaderMessage(headerVersion);
}

//----------------------------------------------------------------------------
igtl::MessageBase::Pointer vtkPlusIgtlMessageFactory::CreateReceiveMessage(const igtl::MessageHeader::Pointer aIgtlMessageHdr) const
{
  if (aIgtlMessageHdr.IsNull())
  {
    LOG_ERROR("Null header sent to factory. Unable to produce a message.");
    return NULL;
  }

  igtl::MessageBase::Pointer aMessageBase;
  try
  {
    aMessageBase = this->IgtlFactory->CreateReceiveMessage(aIgtlMessageHdr);
  }
  catch (std::invalid_argument* e)
  {
    LOG_ERROR("Unable to create message: " << e);
    return NULL;
  }

  if (aMessageBase.IsNull())
  {
    LOG_ERROR("IGTL factory unable to produce message of type:" << aIgtlMessageHdr->GetMessageType());
    return NULL;
  }

  return aMessageBase;
}

//----------------------------------------------------------------------------
igtl::MessageBase::Pointer vtkPlusIgtlMessageFactory::CreateSendMessage(const std::string& messageType, int headerVersion) const
{
  igtl::MessageBase::Pointer aMessageBase;
  try
  {
    aMessageBase = this->IgtlFactory->CreateSendMessage(messageType, headerVersion);
  }
  catch (std::invalid_argument* e)
  {
    LOG_ERROR("Unable to create message: " << e);
    return NULL;
  }
  return aMessageBase;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusIgtlMessageFactory::PackMessages(const PlusIgtlClientInfo& clientInfo, std::vector<igtl::MessageBase::Pointer>& igtlMessages, PlusTrackedFrame& trackedFrame,
    bool packValidTransformsOnly, vtkPlusTransformRepository* transformRepository/*=NULL*/)
{
  int numberOfErrors(0);
  igtlMessages.clear();

  if (transformRepository != NULL)
  {
    transformRepository->SetTransforms(trackedFrame);
  }

  for (std::vector<std::string>::const_iterator messageTypeIterator = clientInfo.IgtlMessageTypes.begin(); messageTypeIterator != clientInfo.IgtlMessageTypes.end(); ++ messageTypeIterator)
  {
    std::string messageType = (*messageTypeIterator);
    igtl::MessageBase::Pointer igtlMessage;
    try
    {
      igtlMessage = this->IgtlFactory->CreateSendMessage(messageType, clientInfo.ClientHeaderVersion);
    }
    catch (std::invalid_argument* e)
    {
      LOG_ERROR("Unable to create message: " << e);
      continue;
    }

    if (igtlMessage.IsNull())
    {
      LOG_ERROR("Failed to pack IGT messages - unable to create instance from message type: " << messageType);
      numberOfErrors++;
      continue;
    }

    // Image message
    if (typeid(*igtlMessage) == typeid(igtl::ImageMessage))
    {
      for (std::vector<PlusIgtlClientInfo::ImageStream>::const_iterator imageStreamIterator = clientInfo.ImageStreams.begin(); imageStreamIterator != clientInfo.ImageStreams.end(); ++imageStreamIterator)
      {
        PlusIgtlClientInfo::ImageStream imageStream = (*imageStreamIterator);

        //Set transform name to [Name]To[CoordinateFrame]
        PlusTransformName imageTransformName = PlusTransformName(imageStream.Name, imageStream.EmbeddedTransformToFrame);

        igtl::Matrix4x4 igtlMatrix;
        if (vtkPlusIgtlMessageCommon::GetIgtlMatrix(igtlMatrix, transformRepository, imageTransformName) != PLUS_SUCCESS)
        {
          LOG_WARNING("Failed to create " << messageType << " message: cannot get image transform");
          numberOfErrors++;
          continue;
        }

        igtl::ImageMessage::Pointer imageMessage = dynamic_cast<igtl::ImageMessage*>(igtlMessage->Clone().GetPointer());
        std::string deviceName = imageTransformName.From() + std::string("_") + imageTransformName.To();
        if (trackedFrame.IsCustomFrameFieldDefined(PlusTrackedFrame::FIELD_FRIENDLY_DEVICE_NAME))
        {
          // Allow overriding of device name with something human readable
          // The transform name is passed in the metadata
          deviceName = trackedFrame.GetCustomFrameField(PlusTrackedFrame::FIELD_FRIENDLY_DEVICE_NAME);
        }
        imageMessage->SetDeviceName(deviceName.c_str());
        if (vtkPlusIgtlMessageCommon::PackImageMessage(imageMessage, trackedFrame, igtlMatrix) != PLUS_SUCCESS)
        {
          LOG_ERROR("Failed to create " << messageType << " message - unable to pack image message");
          numberOfErrors++;
          continue;
        }
        igtlMessages.push_back(imageMessage.GetPointer());
      }
    }
    // Video Stream message
    else if (typeid(*igtlMessage) == typeid(igtl::VideoMessage))
    {
    for (std::vector<PlusIgtlClientInfo::ImageStream>::const_iterator imageStreamIterator = clientInfo.ImageStreams.begin(); imageStreamIterator != clientInfo.ImageStreams.end(); ++imageStreamIterator)
    {
        PlusIgtlClientInfo::ImageStream imageStream = (*imageStreamIterator);

        //Set transform name to [Name]To[CoordinateFrame]
        PlusTransformName imageTransformName = PlusTransformName(imageStream.Name, imageStream.EmbeddedTransformToFrame);
        igtl::VideoMessage::Pointer videoMessage = igtl::VideoMessage::New();//dynamic_cast<igtl::VideoMessage*>(igtlMessage->Clone().GetPointer());
        std::string deviceName = imageTransformName.From() + std::string("_") + imageTransformName.To();
        if (trackedFrame.IsCustomFrameFieldDefined(PlusTrackedFrame::FIELD_FRIENDLY_DEVICE_NAME))
        {
            // Allow overriding of device name with something human readable
            // The transform name is passed in the metadata
            deviceName = trackedFrame.GetCustomFrameField(PlusTrackedFrame::FIELD_FRIENDLY_DEVICE_NAME);
        }
        if (videoStreamEncoderMap.find(deviceName) == videoStreamEncoderMap.end())
        {
          vtkImageData* frameImage = trackedFrame.GetImageData()->GetImage();
          int imageSizePixels[3] = { 0 };
          frameImage->GetDimensions(imageSizePixels);
          float bitRatePercent = 0.01;
#if OpenIGTLink_LINK_X265
          H265Encoder* newEncoder = new H265Encoder();
          newEncoder->SetPicWidthAndHeight(trackedFrame.GetFrameSize()[0], trackedFrame.GetFrameSize()[1]);
          newEncoder->SetLosslessLink(false);
          int bitRateFactor = 5;
          newEncoder->SetRCTaregetBitRate((int)(imageSizePixels[0] * imageSizePixels[1] * 8 * 20 * bitRatePercent)*bitRateFactor);
#elif OpenIGTLink_BUILD_VPX
          VPXEncoder * newEncoder = new VPXEncoder();
          newEncoder->SetPicWidthAndHeight(trackedFrame.GetFrameSize()[0], trackedFrame.GetFrameSize()[1]);
          newEncoder->SetKeyFrameDistance(25);
          newEncoder->SetLosslessLink(false); 
          newEncoder->SetRCTaregetBitRate((int)(imageSizePixels[0]*imageSizePixels[1] * 8 * 20* bitRatePercent));
#endif
          newEncoder->InitializeEncoder();
          newEncoder->SetSpeed(0);
          videoStreamEncoderMap[std::string(deviceName)] = newEncoder;
        }
        videoMessage->SetHeaderVersion(IGTL_HEADER_VERSION_2);
        videoMessage->SetDeviceName(deviceName);
        if (vtkPlusIgtlMessageCommon::PackVideoMessage(videoMessage, trackedFrame, videoStreamEncoderMap[std::string(deviceName)]) != PLUS_SUCCESS)
        {
            LOG_ERROR("Failed to create " << messageType << " message - unable to pack image message");
            numberOfErrors++;
            continue;
        }
        igtlMessages.push_back(videoMessage.GetPointer());
      }
    }
    // Transform message
    else if (typeid(*igtlMessage) == typeid(igtl::TransformMessage))
    {
      for (std::vector<PlusTransformName>::const_iterator transformNameIterator = clientInfo.TransformNames.begin(); transformNameIterator != clientInfo.TransformNames.end(); ++transformNameIterator)
      {
        PlusTransformName transformName = (*transformNameIterator);
        bool isValid = false;
        transformRepository->GetTransformValid(transformName, isValid);

        if (!isValid && packValidTransformsOnly)
        {
          LOG_TRACE("Attempted to send invalid transform over IGT Link when server has prevented sending.");
          continue;
        }

        igtl::Matrix4x4 igtlMatrix;
        vtkPlusIgtlMessageCommon::GetIgtlMatrix(igtlMatrix, transformRepository, transformName);

        igtl::TransformMessage::Pointer transformMessage = dynamic_cast<igtl::TransformMessage*>(igtlMessage->Clone().GetPointer());
        vtkPlusIgtlMessageCommon::PackTransformMessage(transformMessage, transformName, igtlMatrix, trackedFrame.GetTimestamp());
        igtlMessages.push_back(transformMessage.GetPointer());
      }
    }
    // Tracking data message
    else if (typeid(*igtlMessage) == typeid(igtl::TrackingDataMessage))
    {
      if (clientInfo.TDATARequested && clientInfo.LastTDATASentTimeStamp + clientInfo.Resolution < trackedFrame.GetTimestamp())
      {
        std::map<std::string, vtkSmartPointer<vtkMatrix4x4> > transforms;
        for (std::vector<PlusTransformName>::const_iterator transformNameIterator = clientInfo.TransformNames.begin(); transformNameIterator != clientInfo.TransformNames.end(); ++transformNameIterator)
        {
          PlusTransformName transformName = (*transformNameIterator);

          bool isValid = false;
          vtkSmartPointer<vtkMatrix4x4> mat = vtkSmartPointer<vtkMatrix4x4>::New();
          transformRepository->GetTransform(transformName, mat, &isValid);

          if (!isValid)
          {
            continue;
          }

          std::string transformNameStr;
          transformName.GetTransformName(transformNameStr);

          transforms[transformNameStr] = mat;
        }

        igtl::TrackingDataMessage::Pointer trackingDataMessage = dynamic_cast<igtl::TrackingDataMessage*>(igtlMessage->Clone().GetPointer());
        vtkPlusIgtlMessageCommon::PackTrackingDataMessage(trackingDataMessage, transforms, trackedFrame.GetTimestamp());
        igtlMessages.push_back(trackingDataMessage.GetPointer());
      }
    }
    // Position message
    else if (typeid(*igtlMessage) == typeid(igtl::PositionMessage))
    {
      for (std::vector<PlusTransformName>::const_iterator transformNameIterator = clientInfo.TransformNames.begin(); transformNameIterator != clientInfo.TransformNames.end(); ++transformNameIterator)
      {
        /*
          Advantage of using position message type:
          Although equivalent position and orientation can be described with the TRANSFORM data type,
          the POSITION data type has the advantage of smaller data size (19%). It is therefore more suitable for
          pushing high frame-rate data from tracking devices.
        */
        PlusTransformName transformName = (*transformNameIterator);
        igtl::Matrix4x4 igtlMatrix;
        vtkPlusIgtlMessageCommon::GetIgtlMatrix(igtlMatrix, transformRepository, transformName);

        float position[3] = {igtlMatrix[0][3], igtlMatrix[1][3], igtlMatrix[2][3]};
        float quaternion[4] = {0, 0, 0, 1};
        igtl::MatrixToQuaternion(igtlMatrix, quaternion);

        igtl::PositionMessage::Pointer positionMessage = dynamic_cast<igtl::PositionMessage*>(igtlMessage->Clone().GetPointer());
        vtkPlusIgtlMessageCommon::PackPositionMessage(positionMessage, transformName, position, quaternion, trackedFrame.GetTimestamp());
        igtlMessages.push_back(positionMessage.GetPointer());
      }
    }
    // TRACKEDFRAME message
    else if (typeid(*igtlMessage) == typeid(igtl::PlusTrackedFrameMessage))
    {
      igtl::PlusTrackedFrameMessage::Pointer trackedFrameMessage = dynamic_cast<igtl::PlusTrackedFrameMessage*>(igtlMessage->Clone().GetPointer());

      for (auto streamIter = clientInfo.ImageStreams.begin(); streamIter != clientInfo.ImageStreams.end(); ++streamIter)
      {
        // Set transform name to [Name]To[CoordinateFrame]
        PlusTransformName imageTransformName = PlusTransformName(streamIter->Name, streamIter->EmbeddedTransformToFrame);

        vtkSmartPointer<vtkMatrix4x4> mat(vtkSmartPointer<vtkMatrix4x4>::New());
        bool isValid;
        if (transformRepository->GetTransform(imageTransformName, mat, &isValid) != PLUS_SUCCESS)
        {
          LOG_ERROR("Unable to retrieve embedded image transform: " << imageTransformName.GetTransformName() << ".");
          continue;
        }

        for (auto nameIter = clientInfo.TransformNames.begin(); nameIter != clientInfo.TransformNames.end(); ++nameIter)
        {
          vtkSmartPointer<vtkMatrix4x4> matrix(vtkSmartPointer<vtkMatrix4x4>::New());
          transformRepository->GetTransform(*nameIter, matrix, &isValid);
          trackedFrame.SetCustomFrameTransform(*nameIter, matrix);
          trackedFrame.SetCustomFrameTransformStatus(*nameIter, isValid ? FIELD_OK : FIELD_INVALID);
        }

        if (vtkPlusIgtlMessageCommon::PackTrackedFrameMessage(trackedFrameMessage, trackedFrame, mat, clientInfo.TransformNames) != PLUS_SUCCESS)
        {
          LOG_ERROR("Failed to pack IGT messages - unable to pack tracked frame message");
          numberOfErrors++;
          continue;
        }
        igtlMessages.push_back(trackedFrameMessage.GetPointer());
      }
    }
    // USMESSAGE message
    else if (typeid(*igtlMessage) == typeid(igtl::PlusUsMessage))
    {
      igtl::PlusUsMessage::Pointer usMessage = dynamic_cast<igtl::PlusUsMessage*>(igtlMessage->Clone().GetPointer());
      if (vtkPlusIgtlMessageCommon::PackUsMessage(usMessage, trackedFrame) != PLUS_SUCCESS)
      {
        LOG_ERROR("Failed to pack IGT messages - unable to pack US message");
        numberOfErrors++;
        continue;
      }
      igtlMessages.push_back(usMessage.GetPointer());
    }
    // String message
    else if (typeid(*igtlMessage) == typeid(igtl::StringMessage))
    {
      for (std::vector< std::string >::const_iterator stringNameIterator = clientInfo.StringNames.begin(); stringNameIterator != clientInfo.StringNames.end(); ++stringNameIterator)
      {
        const char* stringName = stringNameIterator->c_str();
        const char* stringValue = trackedFrame.GetCustomFrameField(stringName);
        if (stringValue == NULL)
        {
          // no value is available, do not send anything
          continue;
        }
        igtl::StringMessage::Pointer stringMessage = dynamic_cast<igtl::StringMessage*>(igtlMessage->Clone().GetPointer());
        vtkPlusIgtlMessageCommon::PackStringMessage(stringMessage, stringName, stringValue, trackedFrame.GetTimestamp());
        igtlMessages.push_back(stringMessage.GetPointer());
      }
    }
    else if (typeid(*igtlMessage) == typeid(igtl::CommandMessage))
    {
      // Is there any use case for the server sending commands to the client?
      igtl::CommandMessage::Pointer commandMessage = dynamic_cast<igtl::CommandMessage*>(igtlMessage->Clone().GetPointer());
      //vtkPlusIgtlMessageCommon::PackCommandMessage( commandMessage );
      igtlMessages.push_back(commandMessage.GetPointer());
    }
    else
    {
      LOG_WARNING("This message type (" << messageType << ") is not supported!");
    }
  }

  return (numberOfErrors == 0 ? PLUS_SUCCESS : PLUS_FAIL);
}

