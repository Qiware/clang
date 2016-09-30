/* Generated by the protocol buffer compiler.  DO NOT EDIT! */

/* Do not generate deprecated warnings for self */
#ifndef PROTOBUF_C_NO_DEPRECATED
#define PROTOBUF_C_NO_DEPRECATED
#endif

#include "student.pb-c.h"
void   cstudent__init
                     (CStudent         *message)
{
  static CStudent init_value = CSTUDENT__INIT;
  *message = init_value;
}
size_t cstudent__get_packed_size
                     (const CStudent *message)
{
  PROTOBUF_C_ASSERT (message->base.descriptor == &cstudent__descriptor);
  return protobuf_c_message_get_packed_size ((const ProtobufCMessage*)(message));
}
size_t cstudent__pack
                     (const CStudent *message,
                      uint8_t       *out)
{
  PROTOBUF_C_ASSERT (message->base.descriptor == &cstudent__descriptor);
  return protobuf_c_message_pack ((const ProtobufCMessage*)message, out);
}
size_t cstudent__pack_to_buffer
                     (const CStudent *message,
                      ProtobufCBuffer *buffer)
{
  PROTOBUF_C_ASSERT (message->base.descriptor == &cstudent__descriptor);
  return protobuf_c_message_pack_to_buffer ((const ProtobufCMessage*)message, buffer);
}
CStudent *
       cstudent__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data)
{
  return (CStudent *)
     protobuf_c_message_unpack (&cstudent__descriptor,
                                allocator, len, data);
}
void   cstudent__free_unpacked
                     (CStudent *message,
                      ProtobufCAllocator *allocator)
{
  PROTOBUF_C_ASSERT (message->base.descriptor == &cstudent__descriptor);
  protobuf_c_message_free_unpacked ((ProtobufCMessage*)message, allocator);
}
static const ProtobufCFieldDescriptor cstudent__field_descriptors[4] =
{
  {
    "name",
    1,
    PROTOBUF_C_LABEL_OPTIONAL,
    PROTOBUF_C_TYPE_STRING,
    0,   /* quantifier_offset */
    PROTOBUF_C_OFFSETOF(CStudent, name),
    NULL,
    NULL,
    0,            /* packed */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "birthdate",
    2,
    PROTOBUF_C_LABEL_OPTIONAL,
    PROTOBUF_C_TYPE_STRING,
    0,   /* quantifier_offset */
    PROTOBUF_C_OFFSETOF(CStudent, birthdate),
    NULL,
    NULL,
    0,            /* packed */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "age",
    3,
    PROTOBUF_C_LABEL_OPTIONAL,
    PROTOBUF_C_TYPE_UINT32,
    PROTOBUF_C_OFFSETOF(CStudent, has_age),
    PROTOBUF_C_OFFSETOF(CStudent, age),
    NULL,
    NULL,
    0,            /* packed */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "score",
    4,
    PROTOBUF_C_LABEL_OPTIONAL,
    PROTOBUF_C_TYPE_UINT32,
    PROTOBUF_C_OFFSETOF(CStudent, has_score),
    PROTOBUF_C_OFFSETOF(CStudent, score),
    NULL,
    NULL,
    0,            /* packed */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
};
static const unsigned cstudent__field_indices_by_name[] = {
  2,   /* field[2] = age */
  1,   /* field[1] = birthdate */
  0,   /* field[0] = name */
  3,   /* field[3] = score */
};
static const ProtobufCIntRange cstudent__number_ranges[1 + 1] =
{
  { 1, 0 },
  { 0, 4 }
};
const ProtobufCMessageDescriptor cstudent__descriptor =
{
  PROTOBUF_C_MESSAGE_DESCRIPTOR_MAGIC,
  "CStudent",
  "CStudent",
  "CStudent",
  "",
  sizeof(CStudent),
  4,
  cstudent__field_descriptors,
  cstudent__field_indices_by_name,
  1,  cstudent__number_ranges,
  (ProtobufCMessageInit) cstudent__init,
  NULL,NULL,NULL    /* reserved[123] */
};