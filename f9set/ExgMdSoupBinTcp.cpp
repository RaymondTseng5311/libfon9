// \file f9set/ExgMdSoupBinTcp.cpp
#include "f9set/ExgMdSoupBinTcp.hpp"
#include "fon9/StrTo.hpp"
#include "fon9/StrTools.hpp"
#include "fon9/ToStr.hpp"

namespace f9set {

SoupBinTcpClient::~SoupBinTcpClient() {
}
void SoupBinTcpClient::OnSoupLoginAccepted() {}
void SoupBinTcpClient::OnSoupLoginRejected(char) {}
void SoupBinTcpClient::OnSoupServerHeartbeat() {}
void SoupBinTcpClient::OnSoupEndOfSession() {}
void SoupBinTcpClient::OnSoupDebug(fon9::StrView) {}
void SoupBinTcpClient::OnSoupUnknownPacket(char, const void*, unsigned) {}

//--------------------------------------------------------------------------//
static std::string MakeSoupPacket(SoupPacketType pkType, size_t payloadSize) {
   std::string pk(3 + payloadSize, ' ');
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
std::string SoupBinTcpClient::MakeLoginRequest(fon9::StrView username, fon9::StrView password,
                                               fon9::StrView session, uint64_t seqNo) {
   std::string pk = MakeSoupPacket(SoupPacketType::LoginRequest, 6 + 10 + 10 + 20);
   char*       pout = &pk[3];
   PutAlpha(pout, 6, username, false);
   PutAlpha(pout += 6, 10, password, false);
   PutAlpha(pout += 10, 10, session, true);
   char  seqbuf[32];
   char* const seqend = seqbuf + sizeof(seqbuf);
   PutAlpha(pout += 10, 20, fon9::StrView{fon9::UIntToStrRev(seqend, seqNo), seqend}, true);
   return pk;
}
std::string SoupBinTcpClient::MakeClientHeartbeat() {
   return MakeSoupPacket(SoupPacketType::ClientHeartbeat, 0);
}
std::string SoupBinTcpClient::MakeLogoutRequest() {
   return MakeSoupPacket(SoupPacketType::LogoutRequest, 0);
}
//--------------------------------------------------------------------------//
size_t SoupBinTcpClient::FeedBytes(const void* pk, size_t pksz) {
   const fon9::byte* const pbeg = static_cast<const fon9::byte*>(pk);
   size_t                  pos = 0;
   while (pos + sizeof(uint16_t) <= pksz) {
      const unsigned len = fon9::GetBigEndian<uint16_t>(pbeg + pos);
      if (len == 0) {
         this->IsBadPacket_ = true;
         break;
      }
      if (pos + sizeof(uint16_t) + len > pksz)
         break;
      this->OnPacket(pbeg + pos + sizeof(uint16_t), len);
      pos += sizeof(uint16_t) + len;
   }
   return pos;
}
bool SoupBinTcpClient::FeedBuffer(fon9::DcQueue& rxbuf) {
   fon9::byte lenbuf[2];
   for (;;) {
      const void* plen = rxbuf.Peek(lenbuf, sizeof(lenbuf));
      if (plen == nullptr)
         return true;
      const unsigned len = fon9::GetBigEndian<uint16_t>(plen);
      if (len == 0) {
         this->IsBadPacket_ = true;
         return false;
      }
      const size_t pksz = sizeof(uint16_t) + len;
      if (rxbuf.CalcSize() < pksz)
         return true;
      fon9::byte  pkbuf[64 * 1024 + 2];
      const void* pk = rxbuf.Peek(pkbuf, pksz);
      this->OnPacket(static_cast<const fon9::byte*>(pk) + sizeof(uint16_t), len);
      rxbuf.PopConsumed(pksz);
   }
}
void SoupBinTcpClient::OnPacket(const fon9::byte* pk, unsigned pksz) {
   const char           pkType = static_cast<char>(*pk);
   const fon9::byte*    payload = pk + 1;
   const unsigned       payloadsz = pksz - 1;
   switch (static_cast<SoupPacketType>(pkType)) {
   case SoupPacketType::SequencedData:
      this->OnSoupSequencedData(this->NextSeqNo_++, payload, payloadsz);
      break;
   case SoupPacketType::ServerHeartbeat:
      this->OnSoupServerHeartbeat();
      break;
   case SoupPacketType::LoginAccepted:
      if (payloadsz >= 10 + 20) {
         fon9::StrView ses{reinterpret_cast<const char*>(payload), 10};
         fon9::StrView seq{reinterpret_cast<const char*>(payload) + 10, 20};
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
