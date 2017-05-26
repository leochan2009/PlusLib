/*=Plus=header=begin======================================================
  Program: Plus
  Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
  See License.txt for details.
=========================================================Plus=header=end*/ 

#ifndef __vtkPlusIgtlMessageFactory_h
#define __vtkPlusIgtlMessageFactory_h

#include "PlusConfigure.h"
#include "vtkPlusOpenIGTLinkExport.h"
#include "vtkDirectory.h"
#include "vtkObject.h" 
#include "igtlMessageBase.h"
#include "igtlMessageFactory.h"
#include "PlusIgtlClientInfo.h" 

#if OpenIGTLink_BUILD_H264
  #include "H264Encoder.h"
#endif
#if OpenIGTLink_BUILD_VPX
  #include "VPXEncoder.h"
#endif
#if OpenIGTLink_LINK_X265
#include "H265Encoder.h"
#endif

#include <map>
#include <string>

class vtkXMLDataElement; 
class PlusTrackedFrame; 
class vtkPlusTransformRepository;

/*!
  \class vtkPlusIgtlMessageFactory 
  \brief Factory class of supported OpenIGTLink message types

  This class is a factory class of supported OpenIGTLink message types to localize the message creation code.

  \ingroup PlusLibOpenIGTLink
*/ 
class vtkPlusOpenIGTLinkExport vtkPlusIgtlMessageFactory: public vtkObject
{
public:
  static vtkPlusIgtlMessageFactory *New();
  vtkTypeMacro(vtkPlusIgtlMessageFactory,vtkObject);
  virtual void PrintSelf(ostream& os, vtkIndent indent) VTK_OVERRIDE;

  /*! Function pointer for storing New() static methods of igtl::MessageBase classes */ 
  typedef igtl::MessageBase::Pointer (*PointerToMessageBaseNew)(); 

  /*! 
  Get pointer to message type new function, or NULL if the message type not registered 
  Usage: igtl::MessageBase::Pointer message = GetMessageTypeNewPointer("IMAGE")(); 
  */ 
  virtual vtkPlusIgtlMessageFactory::PointerToMessageBaseNew GetMessageTypeNewPointer(const std::string& messageTypeName); 

  /*! Print all supported OpenIGTLink message types */
  virtual void PrintAvailableMessageTypes(ostream& os, vtkIndent indent);

  /// Constructs a message header.
  /// Throws invalid_argument if headerMsg is NULL.
  /// Throws invalid_argument if this->IsValid(headerMsg) returns false.
  /// Creates message, calls InitBuffer()
  igtl::MessageHeader::Pointer CreateHeaderMessage(int headerVersion) const;

  /// Constructs a message from the given populated header.
  /// Throws invalid_argument if headerMsg is NULL.
  /// Throws invalid_argument if this->IsValid(headerMsg) returns false.
  /// Creates message, sets header onto message and calls AllocateBuffer() on the message.
  igtl::MessageBase::Pointer CreateReceiveMessage(igtl::MessageHeader::Pointer headerMsg) const;

  /// Constructs an empty message from the given message type.
  /// Throws invalid_argument if messageType is empty.
  /// Creates message, sets header onto message and calls AllocateBuffer() on the message.
  igtl::MessageBase::Pointer CreateSendMessage(const std::string& messageType, int headerVersion) const;

  /*! 
  Generate and pack IGTL messages from tracked frame
  \param packValidTransformsOnly Control whether or not to pack transform messages if they contain invalid transforms
  \param clientInfo Specifies list of message types and names to generate for a client.
  \param igtMessages Output list for the generated IGTL messages
  \param trackedFrame Input tracked frame data used for IGTL message generation 
  \param transformRepository Transform repository used for computing the selected transforms 
  */ 
  PlusStatus PackMessages(const PlusIgtlClientInfo& clientInfo, std::vector<igtl::MessageBase::Pointer>& igtMessages, PlusTrackedFrame& trackedFrame, 
    bool packValidTransformsOnly, vtkPlusTransformRepository* transformRepository=NULL); 

protected:
  vtkPlusIgtlMessageFactory();
  virtual ~vtkPlusIgtlMessageFactory();

  igtl::MessageFactory::Pointer IgtlFactory;

private:
  vtkPlusIgtlMessageFactory(const vtkPlusIgtlMessageFactory&);
  void operator=(const vtkPlusIgtlMessageFactory&);
  char* configFile;
  std::map<std::string, GenericEncoder*> videoStreamEncoderMap;

}; 

#endif 