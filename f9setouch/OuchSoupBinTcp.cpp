// \file f9setouch/OuchSoupBinTcp.cpp
#include "f9setouch/OuchSoupBinTcp.hpp"
#include "fon9/Endian.hpp"
#include "fon9/StrTo.hpp"
#include "fon9/StrTools.hpp"
#include "fon9/ToStr.hpp"

namespace f9setouch {

static std::string MakeSoupPacket(SoupPacketType pkType, size_t payloadSize) {
   std::string pk(kSoupHeadSize + payloadSize, ' ');
   fon9::PutBigEndian(&pk[0], static_cast<uint16_t>(payloadSize + 1));
   pk[2] = static_cast<char>(pkType);
   return pk;
}
static void PutAlpha(char* dst, size_t dstsz, fon9::StrView src, bool isRightJustified) {
   size_t srcsz = src.size();
   if (srcsz > dstsz)
      srcsz = dstsz;
   memset(dst, ' ', dstsz);
   if (srcsz > 0)
      memcpy(isRightJustified ? (dst + dstsz - srcsz) : dst, src.begin(), srcsz);
}
f9setouch_API std::string MakeSoupLoginRequest(fon9::StrView username, fon9::StrView password,
                                               fon9::StrView session, uint64_t seqNo) {
   std::string pk = MakeSoupPacket(SoupPacketType::LoginRequest,
                                   kSoupUsernameSize + kSoupPasswordSize + kSoupSessionSize + kSoupSeqNoSize);
   char* pout = &pk[kSoupHeadSize];
   PutAlpha(pout, kSoupUsernameSize, username, false);
   PutAlpha(pout += kSoupUsernameSize, kSoupPasswordSize, password, false);
   PutAlpha(pout += kSoupPasswordSize, kSoupSessionSize, session, true);
   char        seqbuf[32];
   char* const seqend = seqbuf + sizeof(seqbuf);
   PutAlpha(pout += kSoupSessionSize, kSoupSeqNoSize, fon9::StrView{fon9::UIntToStrRev(seqend, seqNo), seqend}, true);
   return pk;
}
f9setouch_API std::string MakeSoupClientHeartbeat() {
   return MakeSoupPacket(SoupPacketType::ClientHeartbeat, 0);
}
f9setouch_API std::string MakeSoupLogoutRequest() {
   return MakeSoupPacket(SoupPacketType::LogoutRequest, 0);
}
f9setouch_API void RevPutSoupUnsequencedHead(fon9::RevBufferList& rbuf) {
   const size_t msgsz = fon9::CalcDataSize(rbuf.cfront());
   char*        phead = rbuf.AllocPrefix(kSoupHeadSize) - kSoupHeadSize;
   fon9::PutBigEndian(phead, static_cast<uint16_t>(msgsz + 1));
   phead[2] = static_cast<char>(SoupPacketType::UnsequencedData);
   rbuf.SetPrefixUsed(phead);
}

//--------------------------------------------------------------------------//

OuchSoupClient::~OuchSoupClient() {
}
void OuchSoupClient::OnSoupServerHeartbeat() {
}
void OuchSoupClient::OnSoupDebug(fon9::StrView) {
}
void OuchSoupClient::OnSoupUnknownPacket(char, const void*, unsigned) {
}

bool OuchSoupClient::FeedBuffer(fon9::DcQueue& rxbuf) {
   fon9::byte lenbuf[2];
   for (;;) {
      const void* plen = rxbuf.Peek(lenbuf, sizeof(lenbuf));
      if (plen == nullptr)
         return true;
      const unsigned len = fon9::GetBigEndian<uint16_t>(plen);
      if (len == 0)
         return false;
      const size_t pksz = sizeof(uint16_t) + len;
      fon9::byte   pkbuf[64 * 1024 + 2];
      const void*  pk = rxbuf.Peek(pkbuf, pksz);
      if (pk == nullptr)
         return true;
      this->OnPacket(static_cast<const fon9::byte*>(pk) + sizeof(uint16_t), len);
      rxbuf.PopConsumed(pksz);
   }
}
void OuchSoupClient::OnPacket(const fon9::byte* pk, unsigned pksz) {
   const char        pkType = static_cast<char>(*pk);
   const fon9::byte* payload = pk + 1;
   const unsigned    payloadsz = pksz - 1;
   switch (static_cast<SoupPacketType>(pkType)) {
   case SoupPacketType::SequencedData:
      this->OnSoupSequencedData(this->NextSeqNo_++, payload, payloadsz);
      break;
   case SoupPacketType::ServerHeartbeat:
      this->OnSoupServerHeartbeat();
      break;
   case SoupPacketType::LoginAccepted:
      if (payloadsz >= kSoupSessionSize + kSoupSeqNoSize) {
         fon9::StrView ses{reinterpret_cast<const char*>(payload), kSoupSessionSize};
         fon9::StrView seq{reinterpret_cast<const char*>(payload) + kSoupSessionSize, kSoupSeqNoSize};
         this->Session_ = fon9::StrTrim(&ses).ToString();
         this->NextSeqNo_ = fon9::StrTo(fon9::StrTrim(&seq), uint64_t{0});
         this->IsLoggedIn_ = true;
         this->OnSoupLoginAccepted();
         break;
      }
      this->OnSoupUnknownPacket(pkType, payload, payloadsz);
      break;
   case SoupPacketType::LoginRejected:
      this->OnSoupLoginRejected(payloadsz > 0 ? static_cast<char>(*payload) : ' ');
      break;
   case SoupPacketType::EndOfSession:
      this->OnSoupEndOfSession();
      break;
   case SoupPacketType::Debug:
      this->OnSoupDebug(fon9::StrView{reinterpret_cast<const char*>(payload), payloadsz});
      break;
   default:
      this->OnSoupUnknownPacket(pkType, payload, payloadsz);
      break;
   }
}

} // namespaces
