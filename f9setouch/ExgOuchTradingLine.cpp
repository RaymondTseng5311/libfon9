// \file f9setouch/ExgOuchTradingLine.cpp
#include "f9setouch/ExgOuchTradingLine.hpp"
#include "f9setouch/OuchFmtStr.hpp"
#include "fon9/io/Device.hpp"
#include "fon9/Log.hpp"

namespace f9setouch {

ExgOuchTradingLine::ExgOuchTradingLine(ExgOuchLineMgr& lineMgr, const ExgOuchLineArgs& lineArgs, std::string logFileName)
   : baseTradingLine(lineArgs.LineFc_)
   , FlowCounter_{lineArgs.FcArgs_}
   , LineMgr_(lineMgr)
   , LineArgs_(lineArgs) {
   this->SetNextSeqNo(lineArgs.SeqNo_);
   if (!logFileName.empty()) {
      this->Log_ = fon9::LogFileAppender::Make();
      auto res = this->Log_->OpenImmediately(logFileName, fon9::FileMode::Append | fon9::FileMode::CreatePath);
      if (res.IsError()) {
         fon9_LOG_WARN("ExgOuchTradingLine.OpenLog|fname=", logFileName, '|', res);
         this->Log_.reset();
      }
   }
}
ExgOuchTradingLine::~ExgOuchTradingLine() {
}

//--------------------------------------------------------------------------//

void ExgOuchTradingLine::CheckApBroken() {
   const LineSt bfst = this->LineSt_;
   this->LineSt_ = LineSt::Closed;
   this->ResetLogin();
   if (bfst == LineSt::ApReady)
      this->LineMgr_.OnTradingLineBroken(*this);
}
void ExgOuchTradingLine::AsyncClose(std::string cause) {
   this->CheckApBroken();
   this->Dev_->AsyncLingerClose(std::move(cause));
}
/// rbuf 已填妥內容(必須以 '\n' 結尾, 也就是要先 RevPrint(rbuf, '\n')), 在此加上時間後寫入 log.
void ExgOuchTradingLine::WriteLog(fon9::RevBufferList&& rbuf, fon9::TimeStamp now) {
   if (!this->Log_)
      return;
   fon9::RevPut_Date_Time_us(rbuf, now + fon9::GetLocalTimeZoneOffset());
   this->Log_->Append(rbuf.MoveOut());
}
void ExgOuchTradingLine::WriteLogLinkSt(const fon9::io::StateUpdatedArgs& e) {
   if (!this->Log_)
      return;
   fon9::RevBufferList rbuf{128};
   fon9::RevPrint(rbuf, '\n');
   fon9::RevPrint(rbuf, "|LinkSt|", fon9::io::GetStateStr(e.State_), '|', e.Info_);
   this->WriteLog(std::move(rbuf), fon9::UtcNow());
}
void ExgOuchTradingLine::SendSoup(const std::string& pk, fon9::TimeStamp now) {
   this->Dev_->Send(pk.data(), pk.size());
   this->LastTxTime_ = now;
}

//--------------------------------------------------------------------------//

void ExgOuchTradingLine::OnDevice_Initialized(fon9::io::Device& dev) {
   this->Dev_ = &dev;
}
void ExgOuchTradingLine::OnDevice_StateChanged(fon9::io::Device& dev, const fon9::io::StateChangedArgs& e) {
   (void)dev;
   this->WriteLogLinkSt(e.After_);
   if (e.BeforeState_ == fon9::io::State::LinkReady)
      this->CheckApBroken();
}
void ExgOuchTradingLine::OnDevice_StateUpdated(fon9::io::Device& dev, const fon9::io::StateUpdatedArgs& e) {
   (void)dev;
   this->WriteLogLinkSt(e);
}
fon9::io::RecvBufferSize ExgOuchTradingLine::OnDevice_LinkReady(fon9::io::Device& dev) {
   // 交易所規定: 連線後 1 秒內必須登入, 所以連線成功立即送出 Login Request.
   // 重新連線時: 使用上次登入的 Session, 及下一個要收的序號.
   this->ResetLogin();
   this->LineSt_ = LineSt::LoggingIn;
   const fon9::TimeStamp now = fon9::UtcNow();
   this->LastRxTime_ = now;
   const fon9::StrView session = this->GetSession().empty()
      ? fon9::StrView{&this->LineArgs_.Session_}
      : fon9::StrView{&this->GetSession()};
   if (this->Log_) {
      fon9::RevBufferList rbuf{128};
      fon9::RevPrint(rbuf, '\n');
      fon9::RevPrint(rbuf, "|Tx|Login|user=", this->LineArgs_.User_,
                     "|session=", session, "|seq=", this->GetNextSeqNo());
      this->WriteLog(std::move(rbuf), now);
   }
   this->SendSoup(MakeSoupLoginRequest(&this->LineArgs_.User_, &this->LineArgs_.Pass_,
                                       session, this->GetNextSeqNo()),
                  now);
   dev.CommonTimerRunAfter(this->LineArgs_.HbInterval_);
   return fon9::io::RecvBufferSize::Default;
}
fon9::io::RecvBufferSize ExgOuchTradingLine::OnDevice_Recv(fon9::io::Device& dev, fon9::DcQueue& rxbuf) {
   (void)dev;
   this->LastRxTime_ = fon9::UtcNow();
   if (fon9_LIKELY(this->FeedBuffer(rxbuf)))
      return fon9::io::RecvBufferSize::Default;
   this->AsyncClose("SoupBinTCP: bad packet.");
   return fon9::io::RecvBufferSize::CloseRecv;
}
void ExgOuchTradingLine::OnDevice_CommonTimer(fon9::io::Device& dev, fon9::TimeStamp now) {
   if (this->LineSt_ == LineSt::Closed)
      return;
   if (now - this->LastRxTime_ >= this->LineArgs_.RxTimeout_) {
      this->AsyncClose(this->LineSt_ == LineSt::LoggingIn ? "SoupBinTCP: Login timeout." : "SoupBinTCP: Rx timeout.");
      return;
   }
   fon9::TimeInterval tiNext = this->LineArgs_.HbInterval_ - (now - this->LastTxTime_);
   if (tiNext.GetOrigValue() <= 0) {
      this->SendSoup(MakeSoupClientHeartbeat(), now);
      tiNext = this->LineArgs_.HbInterval_;
   }
   dev.CommonTimerRunAfter(tiNext);
}

//--------------------------------------------------------------------------//

void ExgOuchTradingLine::OnSoupLoginAccepted() {
   this->LineSt_ = LineSt::ApReady;
   fon9::RevBufferList rbuf{128};
   fon9::RevPrint(rbuf, "ApReady|session=", this->GetSession(), "|seq=", this->GetNextSeqNo());
   const std::string msg = fon9::BufferTo<std::string>(rbuf.MoveOut());
   if (this->Log_) {
      fon9::RevPrint(rbuf, "|Rx|LoginAccepted|", msg, '\n');
      this->WriteLog(std::move(rbuf), this->LastRxTime_);
   }
   this->LineMgr_.OnSession_StateUpdated(*this->Dev_, &msg, fon9::LogLevel::Info);
   this->LineMgr_.OnTradingLineReady(*this);
}
void ExgOuchTradingLine::OnSoupLoginRejected(char reason) {
   // 'A' = Not Authorised; 'S' = Session not available.
   std::string cause = fon9::RevPrintTo<std::string>("SoupBinTCP: Login rejected:", reason,
                                                     reason == 'A' ? "(Not authorised)"
                                                     : reason == 'S' ? "(Session not available)" : "");
   if (this->Log_) {
      fon9::RevBufferList rbuf{128};
      fon9::RevPrint(rbuf, '\n');
      fon9::RevPrint(rbuf, "|Rx|", cause);
      this->WriteLog(std::move(rbuf), this->LastRxTime_);
   }
   this->AsyncClose(std::move(cause));
}
void ExgOuchTradingLine::OnSoupEndOfSession() {
   if (this->Log_) {
      fon9::RevBufferList rbuf{64};
      fon9::RevPrint(rbuf, '\n');
      fon9::RevPrint(rbuf, "|Rx|EndOfSession");
      this->WriteLog(std::move(rbuf), this->LastRxTime_);
   }
   this->AsyncClose("SoupBinTCP: End of session.");
}
void ExgOuchTradingLine::OnSoupDebug(fon9::StrView text) {
   if (this->Log_) {
      fon9::RevBufferList rbuf{128};
      fon9::RevPrint(rbuf, '\n');
      fon9::RevPrint(rbuf, "|Rx|Debug|", text);
      this->WriteLog(std::move(rbuf), this->LastRxTime_);
   }
}
void ExgOuchTradingLine::OnSoupUnknownPacket(char pkType, const void* payload, unsigned payloadsz) {
   (void)payload;
   if (this->Log_) {
      fon9::RevBufferList rbuf{64};
      fon9::RevPrint(rbuf, '\n');
      fon9::RevPrint(rbuf, "|Rx|UnknownSoup|type=", pkType, "|sz=", payloadsz);
      this->WriteLog(std::move(rbuf), this->LastRxTime_);
   }
}
void ExgOuchTradingLine::OnSoupSequencedData(uint64_t seqNo, const void* msg, unsigned msgsz) {
   if (this->Log_) {
      fon9::RevBufferList rbuf{256};
      fon9::RevPrint(rbuf, '\n');
      RevPrintOuchOutbound(rbuf, msg, msgsz);
      fon9::RevPrint(rbuf, "|Rx|", seqNo, '|');
      this->WriteLog(std::move(rbuf), this->LastRxTime_);
   }
   this->OnOuchReport(seqNo, msg, msgsz);
}

template <class Msg>
static const Msg* CastRpt(const void* msg, unsigned msgsz) {
   // 長度比預期長: 可能是交易所在尾端增加了欄位, 仍可使用.
   return msgsz >= sizeof(Msg) ? static_cast<const Msg*>(msg) : nullptr;
}
void ExgOuchTradingLine::OnOuchReport(uint64_t seqNo, const void* msg, unsigned msgsz) {
   ExgOuchRptHandler* rpt = this->LineMgr_.GetRptHandler();
   if (rpt == nullptr || msgsz == 0)
      return;
   switch (static_cast<OuchMsgType>(*static_cast<const char*>(msg))) {
   case OuchMsgType::OrderAccepted:
      if (auto m = CastRpt<OuchOrderAccepted>(msg, msgsz))
         return rpt->OnOuchAccepted(*this, seqNo, *m);
      break;
   case OuchMsgType::OrderRejected:
      if (auto m = CastRpt<OuchOrderRejected>(msg, msgsz))
         return rpt->OnOuchRejected(*this, seqNo, *m);
      break;
   case OuchMsgType::OrderReplaced:
      if (auto m = CastRpt<OuchOrderReplaced>(msg, msgsz))
         return rpt->OnOuchReplaced(*this, seqNo, *m);
      break;
   case OuchMsgType::OrderCancelled:
      if (auto m = CastRpt<OuchOrderCancelled>(msg, msgsz))
         return rpt->OnOuchCancelled(*this, seqNo, *m);
      break;
   case OuchMsgType::OrderExecuted:
      if (auto m = CastRpt<OuchOrderExecuted>(msg, msgsz))
         return rpt->OnOuchExecuted(*this, seqNo, *m);
      break;
   default:
      break;
   }
   rpt->OnOuchUnknown(*this, seqNo, msg, msgsz);
}

//--------------------------------------------------------------------------//

template <class Msg>
static Msg& AllocMsg(fon9::RevBufferList& rbuf) {
   char* pmsg = rbuf.AllocPrefix(sizeof(Msg)) - sizeof(Msg);
   rbuf.SetPrefixUsed(pmsg);
   memset(pmsg, ' ', sizeof(Msg));
   return *reinterpret_cast<Msg*>(pmsg);
}
ExgOuchTradingLine::SendResult ExgOuchTradingLine::SendRequest(f9fmkt::TradingRequest& req) {
   ExgOuchRequest* oreq = dynamic_cast<ExgOuchRequest*>(&req);
   if (oreq == nullptr)
      return SendResult::RejectRequest;
   if (fon9_UNLIKELY(this->LineSt_ != LineSt::ApReady))
      return SendResult::Broken;
   // 先檢查要求內容, 再檢查流量: 避免被拒絕的要求占用流量.
   const bool isReplace = oreq->IsReplace();
   switch (oreq->RxKind()) {
   case f9fmkt_RxKind_RequestNew:
      break;
   case f9fmkt_RxKind_RequestChgQty:
   case f9fmkt_RxKind_RequestChgPri:
      if (oreq->ExistingToken_.empty1st())
         return SendResult::RejectRequest;
      break;
   case f9fmkt_RxKind_RequestDelete:
      if (oreq->OrderToken_.empty1st())
         return SendResult::RejectRequest;
      break;
   default:
      return SendResult::NotSupport;
   }
   const fon9::TimeStamp now = fon9::UtcNow();
   const auto ti = this->FlowCounter_.Fetch(now);
   if (ti.GetOrigValue() > 0) // 流量管制: 須等候的時間, 等時間過了之後才可繼續傳送.
      return f9fmkt::ToFlowControlResult(ti);
   if (oreq->RxKind() != f9fmkt_RxKind_RequestDelete && oreq->OrderToken_.empty1st())
      oreq->OrderToken_ = this->LineMgr_.TokenGen_.Fetch();

   fon9::RevBufferList rbuf{sizeof(OuchOrderReplaced) + kSoupHeadSize};
   if (isReplace) {
      OuchReplaceOrder& m = AllocMsg<OuchReplaceOrder>(rbuf);
      m.MsgType_ = static_cast<char>(OuchMsgType::ReplaceOrder);
      m.ExistingOrderToken_ = oreq->ExistingToken_;
      m.ReplacementOrderToken_ = oreq->OrderToken_;
      m.Quantity_.Put(oreq->Qty_);
      m.Price_.Put(oreq->Pri_);
      m.OpenClose_.Put(0);
      m.ClientAccount_ = oreq->ClientAccount_;
      m.CustomerInfo_ = oreq->CustomerInfo_;
      m.ExchangeInfo_ = oreq->ExchangeInfo_;
      m.RegulatoryData_ = oreq->RegulatoryData_;
      m.ShortSellQuantity_.Put(oreq->ShortSellQty_);
      m.MinimumAcceptableQuantity_.Put(oreq->MinAcceptQty_);
   }
   else if (oreq->RxKind() == f9fmkt_RxKind_RequestDelete) {
      OuchCancelOrder& m = AllocMsg<OuchCancelOrder>(rbuf);
      m.MsgType_ = static_cast<char>(OuchMsgType::CancelOrder);
      m.OrderToken_ = oreq->OrderToken_;
   }
   else {
      OuchEnterOrder& m = AllocMsg<OuchEnterOrder>(rbuf);
      m.MsgType_ = static_cast<char>(OuchMsgType::EnterOrder);
      m.OrderToken_ = oreq->OrderToken_;
      m.OrderBookId_.Put(oreq->OrderBookId_);
      m.Side_ = static_cast<char>(oreq->Side_);
      m.Quantity_.Put(oreq->Qty_);
      m.Price_.Put(oreq->Pri_);
      m.TimeInForce_.Put(static_cast<uint8_t>(oreq->TimeInForce_));
      m.OpenClose_.Put(0);
      m.ClientAccount_ = oreq->ClientAccount_;
      m.CustomerInfo_ = oreq->CustomerInfo_;
      m.ExchangeInfo_ = oreq->ExchangeInfo_;
      m.ClearingParticipant_ = oreq->ClearingParticipant_;
      m.CrossingKey_.Put(0);
      m.RegulatoryData_ = oreq->RegulatoryData_;
      m.OrderType_ = static_cast<char>(oreq->OrderType_);
      m.ShortSellQuantity_.Put(oreq->ShortSellQty_);
      m.MinimumAcceptableQuantity_.Put(oreq->MinAcceptQty_);
   }
   if (this->Log_) {
      fon9::RevBufferList lbuf{256};
      fon9::RevPrint(lbuf, '\n');
      RevPrintOuchInbound(lbuf, rbuf.GetCurrent(), static_cast<unsigned>(fon9::CalcDataSize(rbuf.cfront())));
      fon9::RevPrint(lbuf, "|Tx|");
      this->WriteLog(std::move(lbuf), now);
   }
   RevPutSoupUnsequencedHead(rbuf);
   this->Dev_->Send(rbuf.MoveOut());
   this->LastTxTime_ = now;
   return SendResult::Sent;
}

} // namespaces
