// \file f9set/ExgMdSoupBinTcp.hpp
//
// SoupBinTCP client 端封包處理(GLIMPSE 快照使用).
// 參考: ASX Trade ITCH Specification, Appendix 3 - SoupBinTCP Protocol.
//
#ifndef __f9set_ExgMdSoupBinTcp_hpp__
#define __f9set_ExgMdSoupBinTcp_hpp__
#include "f9set/ExgMdItchFmt.hpp"
#include "fon9/buffer/DcQueue.hpp"

fon9_BEFORE_INCLUDE_STD
#include <string>
fon9_AFTER_INCLUDE_STD

namespace f9set {

enum class SoupPacketType : char {
   // ----- Server => Client
   Debug = '+',
   LoginAccepted = 'A',
   LoginRejected = 'J',
   SequencedData = 'S',
   ServerHeartbeat = 'H',
   EndOfSession = 'Z',
   // ----- Client => Server
   LoginRequest = 'L',
   UnsequencedData = 'U',
   ClientHeartbeat = 'R',
   LogoutRequest = 'O',
};

/// SoupBinTCP 封包: [Length(2, big-endian, 不含自己)][PacketType(1)][Payload].
/// - 解析 Server 送來的封包: FeedBuffer();
/// - 建立 Client 要送出的封包: MakeLoginRequest(), MakeClientHeartbeat(), MakeLogoutRequest();
class f9set_API SoupBinTcpClient {
   fon9_NON_COPY_NON_MOVE(SoupBinTcpClient);
public:
   SoupBinTcpClient() = default;
   virtual ~SoupBinTcpClient();

   /// 解析 rxbuf 裡面完整的封包, 不完整的部分留在 rxbuf.
   /// \retval false 封包有誤(例如: 長度為 0), 應斷線.
   bool FeedBuffer(fon9::DcQueue& rxbuf);
   /// 解析 [pk, pk + pksz) 之內完整的封包.
   /// \retval 已處理的資料量; 若 < pksz 表示最後一個封包不完整, 或封包有誤(IsBadPacket()).
   size_t FeedBytes(const void* pk, size_t pksz);
   bool IsBadPacket() const {
      return this->IsBadPacket_;
   }

   /// username 最多 6 碼, password 最多 10 碼, 不足補空白.
   /// session 空白表示目前的 session; seqNo 為下一個要收的序號(GLIMPSE 必須為 1).
   static std::string MakeLoginRequest(fon9::StrView username, fon9::StrView password,
                                       fon9::StrView session, uint64_t seqNo);
   static std::string MakeClientHeartbeat();
   static std::string MakeLogoutRequest();

   /// 登入成功後的 Session 及下一個 Sequenced Data 的序號.
   const std::string& GetSession() const {
      return this->Session_;
   }
   uint64_t GetNextSeqNo() const {
      return this->NextSeqNo_;
   }
   bool IsLoggedIn() const {
      return this->IsLoggedIn_;
   }
   /// 重新連線前呼叫.
   void ResetLogin() {
      this->IsLoggedIn_ = false;
      this->IsBadPacket_ = false;
   }

protected:
   virtual void OnSoupLoginAccepted();
   /// reason: 'A' = Not Authorised; 'S' = Session not available.
   virtual void OnSoupLoginRejected(char reason);
   virtual void OnSoupSequencedData(uint64_t seqNo, const void* msg, unsigned msgsz) = 0;
   virtual void OnSoupServerHeartbeat();
   virtual void OnSoupEndOfSession();
   virtual void OnSoupDebug(fon9::StrView text);
   virtual void OnSoupUnknownPacket(char pkType, const void* payload, unsigned payloadsz);

private:
   std::string Session_;
   uint64_t    NextSeqNo_{0};
   bool        IsLoggedIn_{false};
   bool        IsBadPacket_{false};
   void OnPacket(const fon9::byte* pk, unsigned pksz);
};

} // namespaces
#endif//__f9set_ExgMdSoupBinTcp_hpp__
