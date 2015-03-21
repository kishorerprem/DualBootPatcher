// automatically generated by the FlatBuffers compiler, do not modify

#ifndef FLATBUFFERS_GENERATED_GETVERSION_MBTOOL_DAEMON_V2_H_
#define FLATBUFFERS_GENERATED_GETVERSION_MBTOOL_DAEMON_V2_H_

#include "flatbuffers/flatbuffers.h"


namespace mbtool {
namespace daemon {
namespace v2 {

struct GetVersionRequest;
struct GetVersionResponse;

struct GetVersionRequest : private flatbuffers::Table {
  bool Verify(flatbuffers::Verifier &verifier) const {
    return VerifyTableStart(verifier) &&
           verifier.EndTable();
  }
};

struct GetVersionRequestBuilder {
  flatbuffers::FlatBufferBuilder &fbb_;
  flatbuffers::uoffset_t start_;
  GetVersionRequestBuilder(flatbuffers::FlatBufferBuilder &_fbb) : fbb_(_fbb) { start_ = fbb_.StartTable(); }
  GetVersionRequestBuilder &operator=(const GetVersionRequestBuilder &);
  flatbuffers::Offset<GetVersionRequest> Finish() {
    auto o = flatbuffers::Offset<GetVersionRequest>(fbb_.EndTable(start_, 0));
    return o;
  }
};

inline flatbuffers::Offset<GetVersionRequest> CreateGetVersionRequest(flatbuffers::FlatBufferBuilder &_fbb) {
  GetVersionRequestBuilder builder_(_fbb);
  return builder_.Finish();
}

struct GetVersionResponse : private flatbuffers::Table {
  const flatbuffers::String *version() const { return GetPointer<const flatbuffers::String *>(4); }
  bool Verify(flatbuffers::Verifier &verifier) const {
    return VerifyTableStart(verifier) &&
           VerifyField<flatbuffers::uoffset_t>(verifier, 4 /* version */) &&
           verifier.Verify(version()) &&
           verifier.EndTable();
  }
};

struct GetVersionResponseBuilder {
  flatbuffers::FlatBufferBuilder &fbb_;
  flatbuffers::uoffset_t start_;
  void add_version(flatbuffers::Offset<flatbuffers::String> version) { fbb_.AddOffset(4, version); }
  GetVersionResponseBuilder(flatbuffers::FlatBufferBuilder &_fbb) : fbb_(_fbb) { start_ = fbb_.StartTable(); }
  GetVersionResponseBuilder &operator=(const GetVersionResponseBuilder &);
  flatbuffers::Offset<GetVersionResponse> Finish() {
    auto o = flatbuffers::Offset<GetVersionResponse>(fbb_.EndTable(start_, 1));
    return o;
  }
};

inline flatbuffers::Offset<GetVersionResponse> CreateGetVersionResponse(flatbuffers::FlatBufferBuilder &_fbb,
   flatbuffers::Offset<flatbuffers::String> version = 0) {
  GetVersionResponseBuilder builder_(_fbb);
  builder_.add_version(version);
  return builder_.Finish();
}

}  // namespace v2
}  // namespace daemon
}  // namespace mbtool

#endif  // FLATBUFFERS_GENERATED_GETVERSION_MBTOOL_DAEMON_V2_H_
