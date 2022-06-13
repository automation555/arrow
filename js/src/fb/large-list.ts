// automatically generated by the FlatBuffers compiler, do not modify

import * as flatbuffers from 'flatbuffers';

/**
 * Same as List, but with 64-bit offsets, allowing to represent
 * extremely large data values.
 */
export class LargeList {
  bb: flatbuffers.ByteBuffer|null = null;
  bb_pos = 0;
__init(i:number, bb:flatbuffers.ByteBuffer):LargeList {
  this.bb_pos = i;
  this.bb = bb;
  return this;
}

static getRootAsLargeList(bb:flatbuffers.ByteBuffer, obj?:LargeList):LargeList {
  return (obj || new LargeList()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
}

static getSizePrefixedRootAsLargeList(bb:flatbuffers.ByteBuffer, obj?:LargeList):LargeList {
  bb.setPosition(bb.position() + flatbuffers.SIZE_PREFIX_LENGTH);
  return (obj || new LargeList()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
}

static startLargeList(builder:flatbuffers.Builder) {
  builder.startObject(0);
}

static endLargeList(builder:flatbuffers.Builder):flatbuffers.Offset {
  const offset = builder.endObject();
  return offset;
}

static createLargeList(builder:flatbuffers.Builder):flatbuffers.Offset {
  LargeList.startLargeList(builder);
  return LargeList.endLargeList(builder);
}
}
