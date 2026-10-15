// \file f9setouch/OuchSoupBinTcp.hpp
//
// SoupBinTCP 3.0 client 端封包處理(OUCH 下單使用).
// 參考: ASX Trade OUCH Specification, SoupBinTCP Protocol(Genium INET).
//
// 封包格式: [Length(2, big-endian, 不含自己)][PacketType(1)][Payload].
// - Client => Server: Login Request('L'), Unsequenced Data('U' = OUCH inbound), Client Heartbeat('R'), Logout Request('O');
// - Server => Client: Login Accepted('A'), Login Rejected('J'), Sequenced Data('S' = OUCH outbound),
//                     Server Heartbeat('H'), End of Session('Z'), Debug('+');
//
#ifndef __f9setouch_OuchSoupBinTcp_hpp__
#define __f9setouch_OuchSoupBinTcp_hpp__
#include "f9setouch/Config.h"
#include "fon9/buffer/DcQueue.hpp"
#include "fon9/buffer/RevBufferList.hpp"
#include "fon9/StrView.hpp"

fon9_BEFORE_INCLUDE_STD
#include <string>
fon9_AFTER_INCLUDE_STD

namespace f9setouch {

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

/// SoupBinTCP 封包的表頭大小: Length(2) + PacketType(1).
static constexpr unsigned kSoupHeadSize = 3;
/// Login Request 的欄位長度.
static constexpr unsigned kSoupUsernameSize = 6;
static constexpr unsigned kSoupPasswordSize = 10;
static constexpr unsigned kSoupSessionSize = 10;
static constexpr unsigned kSoupSeqNoSize = 20;

/// 建立 Login Request 封包.
/// - username 最多 6 碼, password 最多 10 碼, 左靠右補空白.
/// - session 空白表示「目前的 session」, 右靠左補空白.
/// - seqNo 為下一個要收的 Sequenced Data 序號; 1 表示從頭開始(取得當日全部回報).
f9setouch_API std::string MakeSoupLoginRequest(fon9::StrView username, fon9::StrView password,
                                               fon9::StrView session, uint64_t seqNo);
f9setouch_API std::string MakeSoupClientHeartbeat();
f9setouch_API std::string MakeSoupLogoutRequest();

/// 在 rbuf 前方加上 Unsequenced Data 的 SoupBinTCP 表頭.
/// rbuf 必須已經填好 OUCH 訊息.
f9setouch_API void RevPutSoupUnsequencedHead(fon9::RevBufferList& rbuf);

/// 解析 Server 送來的 SoupBinTCP 封包.
class f9setouch_API OuchSoupClient {
   fon9_NON_COPY_NON_MOVE(OuchSoupClient);
public:
   OuchSoupClient() = default;
   virtual ~OuchSoupClient();

   /// 解析 rxbuf 裡面完整的封包, 不完整的部分留在 rxbuf.
   /// \retval false 封包有誤(例如: 長度為 0), 應斷線.
   bool FeedBuffer(fon9::DcQueue& rxbuf);

   /// 登入成功後的 Session.
   const std::string& GetSession() const {
      return this->Session_;
   }
   /// 下一個 Sequenced Data 的序號.
   /// 登入前為「要求的序號」, 登入成功後由 Login Accepted 決定.
   uint64_t GetNextSeqNo() const {
      return this->NextSeqNo_;
   }
   void SetNextSeqNo(uint64_t seqNo) {
      this->NextSeqNo_ = seqNo;
   }
   bool IsLoggedIn() const {
      return this->IsLoggedIn_;
   }
   /// 重新連線前呼叫, 保留 Session 及 NextSeqNo, 重新登入時接續回報.
   void ResetLogin() {
      this->IsLoggedIn_ = false;
   }

protected:
   virtual void OnSoupLoginAccepted() = 0;
   /// reason: 'A' = Not Authorised; 'S' = Session not available.
   virtual void OnSoupLoginRejected(char reason) = 0;
   /// msg = OUCH outbound 訊息.
   virtual void OnSoupSequencedData(uint64_t seqNo, const void* msg, unsigned msgsz) = 0;
   virtual void OnSoupEndOfSession() = 0;
   virtual void OnSoupServerHeartbeat();
   virtual void OnSoupDebug(fon9::StrView text);
   virtual void OnSoupUnknownPacket(char pkType, const void* payload, unsigned payloadsz);

private:
   std::string Session_;
   uint64_t    NextSeqNo_{1};
   bool        IsLoggedIn_{false};
   void OnPacket(const fon9::byte* pk, unsigned pksz);
};

} // namespaces
#endif//__f9setouch_OuchSoupBinTcp_hpp__
