#include "PlusBkProFocusReceiver.h"
#include "vtkBkProFocusVideoSource.h"

#include "ResearchInterface.h"

//////////// IAcquitisionDataReceiver interface

//----------------------------------------------------------------------------
PlusBkProFocusReceiver::PlusBkProFocusReceiver()
{
  this->frame = NULL;
  this->bmodeFrame = NULL;
  this->decimation = 4; // ignore IQ samples in each line

  this->params.alg= BMODE_DRC_SQRT;
  this->params.n_lines = 0;
  this->params.n_samples = 0;
  this->params.len = 0;
  this->params.min = 0;
  this->params.max = 0;
  this->params.scale = 0;
  this->params.dyn_range = 50;
  this->params.offset = 10;
}

//----------------------------------------------------------------------------
PlusBkProFocusReceiver::~PlusBkProFocusReceiver()
{
}

//----------------------------------------------------------------------------
bool PlusBkProFocusReceiver::Prepare(int samples, int lines, int pitch)
{
	LOG_DEBUG("Prepare: samples"<<samples<<", lines="<<lines<<", pitch="<<pitch);
  // ensure that pointers have been garbage collected
  if(this->frame != NULL)
  {
    _aligned_free(frame);
    frame = NULL;
  }
  if(this->bmodeFrame != NULL)
  {
    _aligned_free(bmodeFrame);
    bmodeFrame = NULL;
  }

  // initialize parameters
  this->params.n_lines = lines;
  
  // TODO: check this, in CuteGrabbie it is simply: this->params.n_samples = samples / 2;
  this->params.n_samples = samples / 2; // subtract 2 due to header

  // the number of the samples must be 16 byte aligned
  this->params.n_samples -= this->params.n_samples % 8; // each sample is 2 bytes, so mod 8

  // compute derived parameters
  bmode_set_params_sqrt(&params);

  // each sample is four bytes
  this->frame = reinterpret_cast<unsigned char*>(_aligned_malloc(4 * this->params.len, 16u));

  // bmode frame reduces two 2x16bit samples to one 8 bit sample, so it is one quarter the size of frame
  this->bmodeFrame = reinterpret_cast<unsigned char*>(_aligned_malloc(this->params.len, 16u));

  // this->resize(this->params.n_samples/this->decimation, min(this->params.n_lines, this->MaxNumLines)); TODO: check if it's needed, it's in grabbie

  return this->frame != NULL && this->bmodeFrame != NULL;
}

//----------------------------------------------------------------------------
bool PlusBkProFocusReceiver::Cleanup()
{
  if(this->frame != NULL)
  {
    _aligned_free(frame);
    frame = NULL;
  }
  if(this->bmodeFrame != NULL)
  {
    _aligned_free(bmodeFrame);
    bmodeFrame = NULL;
  }
  return true;
}

//----------------------------------------------------------------------------
bool PlusBkProFocusReceiver::DataAvailable(int lines, int pitch, void const* frameData)
{
  if(this->frame == NULL || this->bmodeFrame == NULL)
  {
    LOG_DEBUG("DataAvailable received empty frame");
    return false;
  }

  const ResearchInterfaceLineHeader* header = reinterpret_cast<const ResearchInterfaceLineHeader*>(frameData);
  const unsigned char* inputFrame = reinterpret_cast<const unsigned char*>(frameData);

  // decimate received data into frame
  const int bytesPerSample = 2;
  
  int numBmodeLines = 0; // number of bmode lines in this frame
  for(int i = 0; i < this->params.n_lines && numBmodeLines < this->MaxNumLines; ++i)
  {
    const int32_t* currentInputPosition = reinterpret_cast<const int32_t*>(inputFrame + i*pitch);
    header =  reinterpret_cast<const ResearchInterfaceLineHeader*>(currentInputPosition);

    // only show bmode line
    if(header->ModelID == 0 && header->CFM == 0 && header->FFT == 0) // TODO: check this, in CuteGrabbie "header->ModelID == 0" is not commented out
    {
      int32_t* currentOutputPosition = reinterpret_cast<int32_t*>(this->frame + numBmodeLines*this->params.n_samples*bytesPerSample);

      // n_samples is 16 bit samples, but we need to iterate over 32 bit iq samples, 
      // so divide by 2 to get the right number
      for(int j = 0; j < this->params.n_samples /this->decimation; ++j)
      {
        *currentOutputPosition = *currentInputPosition;
        currentInputPosition += this->decimation;
        currentOutputPosition += 1;
      }

      ++numBmodeLines;
    }
  }

  // compute bmode
  if(numBmodeLines > 0)
  {
    int tempLines = this->params.n_lines;
    this->params.n_lines = numBmodeLines;
    bmode_set_params_sqrt(&params);
    bmode_detect_compress_sqrt_16sc_8u(reinterpret_cast<int16_t*>(this->frame), this->bmodeFrame, &(this->params));
    this->params.n_lines = tempLines;
    bmode_set_params_sqrt(&params);

    //cimg_library::CImg<unsigned char> inputImage((const unsigned char*)bmodeFrame, this->params.n_samples / 2, numBmodeLines);
    if (this->CallbackVideoSource!=NULL)
    {
      // the image is stored in memory line-by-line, thus the orientation is FM or FU (not the usual MF or UF)
      int frameSizeInPix[2]={this->params.n_samples/2, std::min(this->params.n_lines, this->MaxNumLines)};      // TODO: check this, it may need to be {this->params.n_samples/2, this->params.n_lines} - from CuteGrabbie
      const int numberOfBitsPerPixel=8;
      this->CallbackVideoSource->NewFrameCallback(bmodeFrame, frameSizeInPix, numberOfBitsPerPixel);
    }
  }

  return true;
}

//----------------------------------------------------------------------------
void PlusBkProFocusReceiver::SetPlusVideoSource(vtkBkProFocusVideoSource *videoSource)
{
  this->CallbackVideoSource = videoSource;
}