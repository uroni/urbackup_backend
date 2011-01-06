/*
 * Copyright (c) 2001-2007 Peter Simons <simons@cryp.to>
 *
 * This software is provided 'as-is', without any express or
 * implied warranty. In no event will the authors be held liable
 * for any damages arising from the use of this software.
 *
 * Copying and distribution of this file, with or without
 * modification, are permitted in any medium without royalty
 * provided the copyright notice and this notice are preserved.
 */

#include "fastcgi.hpp"
#include <iostream>
#include <assert.h>
#include <stdio.h>
#ifndef _WIN32
#include <memory.h>
#endif

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;

enum message_type_t
  { TYPE_BEGIN_REQUEST     =  1
  , TYPE_ABORT_REQUEST     =  2
  , TYPE_END_REQUEST       =  3
  , TYPE_PARAMS            =  4
  , TYPE_STDIN             =  5
  , TYPE_STDOUT            =  6
  , TYPE_STDERR            =  7
  , TYPE_DATA              =  8
  , TYPE_GET_VALUES        =  9
  , TYPE_GET_VALUES_RESULT = 10
  , TYPE_UNKNOWN           = 11
  };

struct Header
{
  uint8_t version;
  uint8_t type;
  uint8_t requestIdB1;
  uint8_t requestIdB0;
  uint8_t contentLengthB1;
  uint8_t contentLengthB0;
  uint8_t paddingLength;
  uint8_t reserved;

  Header()
  {
    memset(this, 0, sizeof(*this));
  }

  Header(message_type_t t, uint16_t id, uint16_t len)
    : version(1), type(t)
    , requestIdB1(id >> 8)
    , requestIdB0(id & 0xff)
    , contentLengthB1(len >> 8)
    , contentLengthB0(len & 0xff)
    , paddingLength(0), reserved(0)
  {
  }
};

struct BeginRequest
{
  uint8_t roleB1;
  uint8_t roleB0;
  uint8_t flags;
  uint8_t reserved[5];
};

static uint8_t const FLAG_KEEP_CONN = 1;

struct EndRequestMsg : public Header
{
  uint8_t appStatusB3;
  uint8_t appStatusB2;
  uint8_t appStatusB1;
  uint8_t appStatusB0;
  uint8_t protocolStatus;
  uint8_t reserved[3];

  EndRequestMsg()
  {
    memset(this, 0, sizeof(*this));
  }

  EndRequestMsg(uint16_t id, uint32_t appStatus, FCGIRequest::protocol_status_t protStatus)
    : Header(TYPE_END_REQUEST, id, sizeof(EndRequestMsg)-sizeof(Header))
    , appStatusB3((appStatus >> 24) & 0xff)
    , appStatusB2((appStatus >> 16) & 0xff)
    , appStatusB1((appStatus >>  8) & 0xff)
    , appStatusB0((appStatus >>  0) & 0xff)
    , protocolStatus(protStatus)
  {
    memset(this->reserved, 0, sizeof(this->reserved));
  }
};

struct UnknownTypeMsg : public Header
{
  uint8_t type;
  uint8_t reserved[7];

  UnknownTypeMsg()
  {
    memset(this, 0, sizeof(*this));
  }

  UnknownTypeMsg(uint8_t unknown_type)
    : Header(TYPE_UNKNOWN, 0, sizeof(UnknownTypeMsg) - sizeof(Header))
    , type(unknown_type)
  {
    memset(this->reserved, 0, sizeof(this->reserved));
  }
};

void FCGIProtocolDriver::process_unknown(uint8_t type)
{
  UnknownTypeMsg msg(type);
  output_cb(&msg, sizeof(UnknownTypeMsg));
}

void FCGIProtocolDriver::process_begin_request(uint16_t id, uint8_t const * buf, uint16_t)
{
  // Check whether we have an open request with that id already and
  // if, throw an exception.

  if (reqmap.find(id) != reqmap.end())
  {
    char tmp[256];
#ifdef _WIN32
	sprintf_s(tmp, "FCGIProtocolDriver received duplicate BEGIN_REQUEST id %u.", id);
#else
    sprintf(tmp, "FCGIProtocolDriver received duplicate BEGIN_REQUEST id %u.", id);
#endif
    throw duplicate_begin_request(tmp);
  }

  // Create a new request instance and store it away. The user may
  // get it after we've read all parameters.

  BeginRequest const * br = reinterpret_cast<BeginRequest const *>(buf);
  reqmap[id] = new FCGIRequest(*this, id,
                              FCGIRequest::role_t((br->roleB1 << 8) + br->roleB0),
                              (br->flags & FLAG_KEEP_CONN) == 1);
}

void FCGIProtocolDriver::process_abort_request(uint16_t id, uint8_t const *, uint16_t)
{
  // Find request instance for this id. Ignore message if non
  // exists, set ignore flag otherwise.

  reqmap_t::iterator req = reqmap.find(id);
  if (req == reqmap.end())
    std::cerr << "FCGIProtocolDriver received ABORT_REQUEST for non-existing id " << id << ". Ignoring."
              << std::endl;
  else
  {
    req->second->aborted = true;
    if (req->second->handler_cb) // Notify the handler associated with this request.
      (*req->second->handler_cb)(req->second);
  }
}

void FCGIProtocolDriver::process_params(uint16_t id, uint8_t const * buf, uint16_t len)
{
  // Find request instance for this id. Ignore message if non
  // exists.

  reqmap_t::iterator req = reqmap.find(id);
  if (req == reqmap.end())
  {
    std::cerr << "FCGIProtocolDriver received PARAMS for non-existing id " << id << ". Ignoring."
              << std::endl;
    return;
  }

  // Is this the last message to come? Then queue the request for
  // the user.

  if (len == 0)
  {
    new_request_queue.push(id);
    return;
  }

  // Process message.

  uint8_t const * const  bufend(buf + len);
  uint32_t               name_len;
  uint32_t               data_len;
  while(buf != bufend)
  {
    if (*buf >> 7 == 0)
      name_len = *(buf++);
    else
    {
      name_len = ((buf[0] & 0x7F) << 24) + (buf[1] << 16) + (buf[2] << 8) + buf[3];
      buf += 4;
    }
    if (*buf >> 7 == 0)
      data_len = *(buf++);
    else
    {
      data_len = ((buf[0] & 0x7F) << 24) + (buf[1] << 16) + (buf[2] << 8) + buf[3];
      buf += 4;
    }
    assert(buf + name_len + data_len <= bufend);
    std::string const name(reinterpret_cast<char const *>(buf), name_len);
    buf += name_len;
    std::string const data(reinterpret_cast<char const *>(buf), data_len);
    buf += data_len;
#ifdef DEBUG_FASTCGI
    std::cerr << "request #" << id << ": FCGIProtocolDriver received PARAM '" << name << "' = '" << data << "'"
              << std::endl;
#endif
    req->second->params[name] = data;
  }
}

void FCGIProtocolDriver::process_stdin(uint16_t id, uint8_t const * buf, uint16_t len)
{
  // Find request instance for this id. Ignore message if non
  // exists.

  reqmap_t::iterator req = reqmap.find(id);
  if (req == reqmap.end())
  {
    std::cerr << "FCGIProtocolDriver received STDIN for non-existing id " << id << ". Ignoring."
              << std::endl;
    return;
  }

  // Is this the last message to come? Then set the eof flag.
  // Otherwise, add the data to the buffer in the request structure.

  if (len == 0)
    req->second->stdin_eof = true;
  else
    req->second->stdin_stream.append((char const *)buf, len);

  // Notify the handler associated with this request.

  if (req->second->handler_cb)
    (*req->second->handler_cb)(req->second);
}

FCGIProtocolDriver::FCGIProtocolDriver(OutputCallback& cb) : output_cb(cb)
{
}

FCGIProtocolDriver::~FCGIProtocolDriver()
{
  for(reqmap_t::iterator i = reqmap.begin(); i != reqmap.end(); ++i)
  {
    delete i->second;
  }
}

void FCGIProtocolDriver::process_input(void const * buf, size_t count)
{
  // Copy data to our own buffer.

  InputBuffer.insert( InputBuffer.end()
                    , static_cast<uint8_t const *>(buf)
                    , static_cast<uint8_t const *>(buf) + count
                    );

  // If there is enough data in the input buffer to contain a
  // header, interpret it.

  while(InputBuffer.size() >= sizeof(Header))
  {
    Header const * hp = reinterpret_cast<Header const *>(&InputBuffer[0]);

    // Check whether our peer speaks the correct protocol version.

    if (hp->version != 1)
    {
      char buf[256];
#ifdef _WIN32
      sprintf_s(buf, "FCGIProtocolDriver cannot handle protocol version %u.", hp->version);
#else
	  sprintf(buf, "FCGIProtocolDriver cannot handle protocol version %u.", hp->version);
#endif
      throw unsupported_fcgi_version(buf);
    }

    // Check whether we have the whole message that follows the
    // headers in our buffer already. If not, we can't process it
    // yet.

    uint16_t msg_len = (hp->contentLengthB1 << 8) + hp->contentLengthB0;
    uint16_t msg_id  = (hp->requestIdB1 << 8) + hp->requestIdB0;

    if (InputBuffer.size() < sizeof(Header)+msg_len+hp->paddingLength)
      return;

    // Process the message. In case an exception arrives here,
    // terminate the request.

    try
    {
#ifdef DEBUG_FASTCGI
      std::cerr << "Received message: id = " << msg_id << ", "
                << "body len = " << msg_len << ", "
                << "type = " << (int)hp->type << std::endl;
#endif
      switch (hp->type)
      {
        case TYPE_BEGIN_REQUEST:
          process_begin_request(msg_id, &InputBuffer[0]+sizeof(Header), msg_len);
          break;

        case TYPE_ABORT_REQUEST:
          process_abort_request(msg_id, &InputBuffer[0]+sizeof(Header), msg_len);
          break;

        case TYPE_PARAMS:
          process_params(msg_id, &InputBuffer[0]+sizeof(Header), msg_len);
          break;

        case TYPE_STDIN:
          process_stdin(msg_id, &InputBuffer[0]+sizeof(Header), msg_len);
          break;

        case TYPE_END_REQUEST:
        case TYPE_STDOUT:
        case TYPE_STDERR:
        case TYPE_DATA:
        case TYPE_GET_VALUES:
        case TYPE_GET_VALUES_RESULT:
        case TYPE_UNKNOWN:
        default:
          process_unknown(hp->type);
      }
    }
    catch(fcgi_io_callback_error const &)
    {
      throw;
    }
    catch(std::exception const & e)
    {
      std::cerr << "Caught exception while processing request #" << msg_id << ": " << e.what() << std::endl;
      terminate_request(msg_id);
    }
    catch(...)
    {
      std::cerr << "Caught unknown exception while processing request #" << msg_id << "." << std::endl;
      terminate_request(msg_id);
    }

    // Remove the message from our buffer and contine processing
    // if there if something left.

    InputBuffer.erase( InputBuffer.begin()
                     , InputBuffer.begin()+sizeof(Header)+msg_len+hp->paddingLength
                     );
  }
}

FCGIRequest* FCGIProtocolDriver::get_request()
{
  if (new_request_queue.empty())
    return 0;

  FCGIRequest* r = reqmap[new_request_queue.front()];
  new_request_queue.pop();
  return r;
}

bool FCGIProtocolDriver::have_active_requests()
{
  if (new_request_queue.empty() && reqmap.empty())
    return false;
  else
    return true;
}

void FCGIProtocolDriver::terminate_request(uint16_t id)
{
  reqmap_t::iterator req;
  req = reqmap.find(id);
  if (req != reqmap.end())
  {
    delete req->second;
    reqmap.erase(req);
  }
}

// Pure virtual destructors must also exist somewhere.

FCGIProtocolDriver::OutputCallback::~OutputCallback()
{
}

FCGIRequest::FCGIRequest(FCGIProtocolDriver& driver_, uint16_t id_, role_t role_, bool kc)
  : id(id_), role(role_), keep_connection(kc), aborted(false), stdin_eof(false)
  , data_eof(false), handler_cb(0), driver(driver_)
{
}

FCGIRequest::~FCGIRequest()
{
  if (handler_cb)
    delete handler_cb;
}

void FCGIRequest::write(const std::string& buf, ostream_type_t stream)
{
  write(buf.data(), buf.size(), stream);
}

void FCGIRequest::write(char const * buf, size_t count, ostream_type_t stream)
{
  if (count > 0xffff)
    throw std::out_of_range("Can't send messages of that size.");
  else if (count == 0)
    return;

  // Construct message.

  Header h(stream == STDOUT ? TYPE_STDOUT : TYPE_STDERR, id, (uint16_t)count);
  driver.output_cb(&h, sizeof(Header));
  driver.output_cb(buf, count);
}

void FCGIRequest::write(const std::string  &tw)
{
	write(tw.data(), tw.size(), STDOUT);
}

void FCGIRequest::end_request(uint32_t appStatus, FCGIRequest::protocol_status_t protStatus)
{
  // Terminate the stdout and stderr stream, and send the
  // end-request message.

  uint8_t buf[64];
  uint8_t * p = buf;

  new(p) Header(TYPE_STDOUT, id, 0);
  p += sizeof(Header);
  new(p) Header(TYPE_STDERR, id, 0);
  p += sizeof(Header);
  new(p) EndRequestMsg(id, appStatus, protStatus);
  p += sizeof(EndRequestMsg);
  driver.output_cb(buf, p - buf);
  driver.terminate_request(id);
}
