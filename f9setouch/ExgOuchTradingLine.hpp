// \file f9setouch/ExgOuchTradingLine.hpp
//
// SET OUCH 下單線路: SoupBinTCP client + OUCH 訊息.
//
#ifndef __f9setouch_ExgOuchTradingLine_hpp__
#define __f9setouch_ExgOuchTradingLine_hpp__
#include "f9setouch/ExgOuchLineMgr.hpp"
#include "f9setouch/OuchSoupBinTcp.hpp"
#include "fon9/io/Session.hpp"
#include "fon9/LogFile.hpp"

namespace f9setouch {

fon9_WARN_DISABLE_PADDING;
/// SET OUCH 下單線路.
/// - 連線成功後立即送出 Login Request(交易所規定 1 秒內必須登入).
/// - 收到 Login Accepted 後, 進入可下單狀態: LineMgr_.OnTradingLineReady(*this);
/// - 斷線後重新連線時, 從上次收到的序號接續(Session 不變); 程式重啟時從 Args.SeqNo 開始.
/// - 超過 Args.Hb 沒送出資料, 則送出 Client Heartbeat; 超過 Args.RxTimeout 沒收到資料, 則斷線.
/// - 回報(Sequenced Data)轉給 LineMgr_.GetRptHandler();
/// - 下單要求必須是 ExgOuchRequest; 線路只負責送出, 不檢查價格、數量、交易時段...
class f9setouch_API ExgOuchTradingLine : public fon9::io::Session
                                       , public f9fmkt::TradingLine
                                       , protected OuchSoupClient {
   fon9_NON_COPY_NON_MOVE(ExgOuchTradingLine);
   using base = fon9::io::Session;
   using baseTradingLine = f9fmkt::TradingLine;

   enum class LineSt {
      Closed,
      LoggingIn,
      ApReady,
   };
   LineSt                     LineSt_{LineSt::Closed};
   fon9::io::Device*          Dev_{};
   fon9::AsyncFileAppenderSP  Log_;
   fon9::TimeStamp            LastRxTime_;
   fon9::TimeStamp            LastTxTime_;
   fon9::FlowCounter          FlowCounter_;

   void CheckApBroken();
   void AsyncClose(std::string cause);
   void WriteLog(fon9::RevBufferList&& rbuf, fon9::TimeStamp now);
   void WriteLogLinkSt(const fon9::io::StateUpdatedArgs& e);
   void SendSoup(const std::string& pk, fon9::TimeStamp now);

protected:
   void OnDevice_Initialized(fon9::io::Device& dev) override;
   void OnDevice_StateChanged(fon9::io::Device& dev, const fon9::io::StateChangedArgs& e) override;
   void OnDevice_StateUpdated(fon9::io::Device& dev, const fon9::io::StateUpdatedArgs& e) override;
   fon9::io::RecvBufferSize OnDevice_LinkReady(fon9::io::Device& dev) override;
   fon9::io::RecvBufferSize OnDevice_Recv(fon9::io::Device& dev, fon9::DcQueue& rxbuf) override;
   void OnDevice_CommonTimer(fon9::io::Device& dev, fon9::TimeStamp now) override;

   void OnSoupLoginAccepted() override;
   void OnSoupLoginRejected(char reason) override;
   void OnSoupSequencedData(uint64_t seqNo, const void* msg, unsigned msgsz) override;
   void OnSoupEndOfSession() override;
   void OnSoupDebug(fon9::StrView text) override;
   void OnSoupUnknownPacket(char pkType, const void* payload, unsigned payloadsz) override;

   /// 依照 msg 的種類及長度, 轉給 RptHandler.
   virtual void OnOuchReport(uint64_t seqNo, const void* msg, unsigned msgsz);

public:
   ExgOuchLineMgr&         LineMgr_;
   const ExgOuchLineArgs   LineArgs_;

   /// logFileName.empty() 表示不記錄 log.
   ExgOuchTradingLine(ExgOuchLineMgr& lineMgr, const ExgOuchLineArgs& lineArgs, std::string logFileName);
   ~ExgOuchTradingLine();

   /// 必須是 ExgOuchRequest, 否則返回 SendResult::RejectRequest;
   /// - New:     送出 Enter Order;
   /// - ChgQty, ChgPri: 送出 Replace Order, 必須有 ExistingToken_;
   /// - Delete:  送出 Cancel Order, 必須有 OrderToken_;
   /// - 若 OrderToken_ 為空白(Delete 除外), 則由 LineMgr_.TokenGen_ 產生, 並填入 req.OrderToken_;
   SendResult SendRequest(f9fmkt::TradingRequest& req) override;

   bool IsApReady() const {
      return this->LineSt_ == LineSt::ApReady;
   }
   /// 登入成功後的 Session.
   const std::string& GetSoupSession() const {
      return this->GetSession();
   }
   /// 下一個回報的序號.
   uint64_t GetNextRptSeqNo() const {
      return this->GetNextSeqNo();
   }
};
fon9_WARN_POP;

} // namespaces
#endif//__f9setouch_ExgOuchTradingLine_hpp__
