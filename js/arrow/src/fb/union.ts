// automatically generated by the FlatBuffers compiler, do not modify

import * as flatbuffers from 'flatbuffers';

import { UnionMode } from './union-mode.js';


/**
 * A union is a complex type with children in Field
 * By default ids in the type vector refer to the offsets in the children
 * optionally typeIds provides an indirection between the child offset and the type id
 * for each child `typeIds[offset]` is the id used in the type vector
 */
export class Union {
  bb: flatbuffers.ByteBuffer|null = null;
  bb_pos = 0;
__init(i:number, bb:flatbuffers.ByteBuffer):Union {
  this.bb_pos = i;
  this.bb = bb;
  return this;
}

static getRootAsUnion(bb:flatbuffers.ByteBuffer, obj?:Union):Union {
  return (obj || new Union()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
}

static getSizePrefixedRootAsUnion(bb:flatbuffers.ByteBuffer, obj?:Union):Union {
  bb.setPosition(bb.position() + flatbuffers.SIZE_PREFIX_LENGTH);
  return (obj || new Union()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
}

mode():UnionMode {
  const offset = this.bb!.__offset(this.bb_pos, 4);
  return offset ? this.bb!.readInt16(this.bb_pos + offset) : UnionMode.Sparse;
}

typeIds(index: number):number|null {
  const offset = this.bb!.__offset(this.bb_pos, 6);
  return offset ? this.bb!.readInt32(this.bb!.__vector(this.bb_pos + offset) + index * 4) : 0;
}

typeIdsLength():number {
  const offset = this.bb!.__offset(this.bb_pos, 6);
  return offset ? this.bb!.__vector_len(this.bb_pos + offset) : 0;
}

typeIdsArray():Int32Array|null {
  const offset = this.bb!.__offset(this.bb_pos, 6);
  return offset ? new Int32Array(this.bb!.bytes().buffer, this.bb!.bytes().byteOffset + this.bb!.__vector(this.bb_pos + offset), this.bb!.__vector_len(this.bb_pos + offset)) : null;
}

static startUnion(builder:flatbuffers.Builder) {
  builder.startObject(2);
}

static addMode(builder:flatbuffers.Builder, mode:UnionMode) {
  builder.addFieldInt16(0, mode, UnionMode.Sparse);
}

static addTypeIds(builder:flatbuffers.Builder, typeIdsOffset:flatbuffers.Offset) {
  builder.addFieldOffset(1, typeIdsOffset, 0);
}

static createTypeIdsVector(builder:flatbuffers.Builder, data:number[]|Int32Array):flatbuffers.Offset;
/**
 * @deprecated This Uint8Array overload will be removed in the future.
 */
static createTypeIdsVector(builder:flatbuffers.Builder, data:number[]|Uint8Array):flatbuffers.Offset;
static createTypeIdsVector(builder:flatbuffers.Builder, data:number[]|Int32Array|Uint8Array):flatbuffers.Offset {
  builder.startVector(4, data.length, 4);
  for (let i = data.length - 1; i >= 0; i--) {
    builder.addInt32(data[i]!);
  }
  return builder.endVector();
}

static startTypeIdsVector(builder:flatbuffers.Builder, numElems:number) {
  builder.startVector(4, numElems, 4);
}

static endUnion(builder:flatbuffers.Builder):flatbuffers.Offset {
  const offset = builder.endObject();
  return offset;
}

static createUnion(builder:flatbuffers.Builder, mode:UnionMode, typeIdsOffset:flatbuffers.Offset):flatbuffers.Offset {
  Union.startUnion(builder);
  Union.addMode(builder, mode);
  Union.addTypeIds(builder, typeIdsOffset);
  return Union.endUnion(builder);
}
}
