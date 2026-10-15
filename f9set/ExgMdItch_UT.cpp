// \file f9set/ExgMdItch_UT.cpp
//
// 測試 SET ITCH(MoldUDP64) 行情解析: 用手工封包驗證 MoldUDP64 序號處理, 訊息分派, 委託簿五檔, 成交, 盤別.
//
// 也可以重播錄製檔: f9setExgMdItch_UT RecordFile
// - 錄製檔格式: 每個 UDP payload 前面加上 2 bytes(big-endian) 的長度.
// - 加上 --save RecordFile 可將本測試的手工封包存成錄製檔.
//
#define _CRT_SECURE_NO_WARNINGS
#include "f9set/ExgMdBookBuilder.hpp"
#include "f9set/ExgMdSoupBinTcp.hpp"
#include "f9set/ExgMdRtsBuilder.hpp"
#include "f9set/ExgMdTestPk.hpp"
#include "fon9/buffer/DcQueueList.hpp"
#include "fon9/TestTools.hpp"
#include <string>
#include <vector>

//--------------------------------------------------------------------------//
struct TestBuilder : public f9set::ExgMdBookBuilder {
   fon9_NON_COPY_NON_MOVE(TestBuilder);
   TestBuilder() = default;

   std::vector<std::string>   Packets_;
   std::vector<f9set::ExgMdDeal> Deals_;
   uint64_t BookChangedCount_{0};
   uint64_t PhaseChangedCount_{0};
   uint64_t StartOfMessagesCount_{0};
   uint64_t EndOfSessionCount_{0};
   uint64_t SessionChangedCount_{0};
   uint64_t LastGapFrom_{0};
   uint64_t LastGapTo_{0};
   uint64_t LastLostFrom_{0};
   uint64_t LastLostTo_{0};
   uint64_t SnapshotDoneCount_{0};
   bool     CanSendRequest_{true};
   std::vector<f9set::MoldUdp64Request> Requests_;

   f9set::MoldUdp64Result Feed(const std::string& pk) {
      this->Packets_.push_back(pk);
      return this->FeedPacket(pk.data(), pk.size());
   }
   void OnBookChanged(f9set::ExgMdOrderBook&, const f9set::ItchMsgHead&) override {
      ++this->BookChangedCount_;
   }
   void OnDeal(f9set::ExgMdOrderBook&, const f9set::ExgMdDeal& deal) override {
      this->Deals_.push_back(deal);
   }
   void OnPhaseChanged(f9set::ExgMdOrderBook&, f9set::SetTradingPhase) override {
      ++this->PhaseChangedCount_;
   }
   void OnStartOfMessages() override {
      ++this->StartOfMessagesCount_;
   }
   void OnMoldGap(uint64_t expectedSeqNo, uint64_t receivedSeqNo) override {
      this->LastGapFrom_ = expectedSeqNo;
      this->LastGapTo_ = receivedSeqNo;
   }
   void OnMoldLost(uint64_t fromSeqNo, uint64_t toSeqNo) override {
      this->LastLostFrom_ = fromSeqNo;
      this->LastLostTo_ = toSeqNo;
   }
   bool OnMoldSendRequest(const f9set::MoldUdp64Request& req) override {
      if (!this->CanSendRequest_)
         return false;
      this->Requests_.push_back(req);
      return true;
   }
   void OnSnapshotDone() override {
      ++this->SnapshotDoneCount_;
   }
   void OnMoldEndOfSession() override {
      ++this->EndOfSessionCount_;
   }
   void OnMoldSessionChanged(const f9set::MoldUdp64Session&) override {
      ++this->SessionChangedCount_;
   }
};

static fon9::fmkt::Pri Pri2(int64_t v) {
   return fon9::fmkt::Pri::Make<2>(v);
}
static bool IsPQ(const fon9::fmkt::PriQty& pq, int64_t priV2, fon9::fmkt::Qty qty) {
   return pq.Pri_ == Pri2(priV2) && pq.Qty_ == qty;
}

static const char kSession[] = "0000000001";
static const uint32_t kPTT = 1001;
static const uint32_t kDELTA = 1002;

//--------------------------------------------------------------------------//
void TestMoldUdp64(TestBuilder& tb) {
   std::cout << "--- MoldUDP64 ---" << std::endl;
   MoldPkBuilder pk1{kSession, 1};
   pk1.Seconds(1792029600); // 2026-10-15 09:00:00 (UTC+7).
   pk1.SystemEvent('O');
   pk1.Directory(kPTT, "PTT", 2, 100);
   pk1.Directory(kDELTA, "DELTA", 2, 100);
   pk1.State(kPTT, "PRE-OPEN1", 123456789);
   const std::string& p1 = pk1.Finish();
   fon9_CheckTestResult("pk1.Ok", tb.Feed(p1) == f9set::MoldUdp64Result::Ok);
   fon9_CheckTestResult("pk1.NextSeqNo=6", tb.GetNextSeqNo() == 6);
   fon9_CheckTestResult("pk1.MsgCount=5", tb.MsgCount_ == 5);
   fon9_CheckTestResult("Seconds", tb.GetItchSeconds() == 1792029600);
   fon9_CheckTestResult("StartOfMessages", tb.StartOfMessagesCount_ == 1);
   fon9_CheckTestResult("Directory.count", tb.GetOrderBookCount() == 2);
   const f9set::ExgMdOrderBook* ptt = tb.GetOrderBook(kPTT);
   fon9_CheckTestResult("Directory.PTT", ptt && ptt->Symbol_ == "PTT" && ptt->PriceDecimals_ == 2
                        && ptt->RoundLotSize_ == 100 && ptt->Isin_ == "TH0000000000");

   // B 路(備援) 送來相同封包: 全部都是重複.
   fon9_CheckTestResult("pk1.Duplicate", tb.Feed(p1) == f9set::MoldUdp64Result::Duplicate);
   fon9_CheckTestResult("pk1.DupMsgCount=5", tb.DupMsgCount_ == 5 && tb.MsgCount_ == 5);

   // Heartbeat: SeqNo = 下一個序號, 沒有缺號.
   MoldPkBuilder hb{kSession, 6};
   fon9_CheckTestResult("Heartbeat", tb.Feed(hb.Raw()) == f9set::MoldUdp64Result::Heartbeat);
   fon9_CheckTestResult("Heartbeat.NoGap", tb.GapCount_ == 0);

   // 部分重複: [5..7], 其中 5 已收過.
   MoldPkBuilder pk2{kSession, 5};
   pk2.State(kPTT, "PRE-OPEN1");
   pk2.State(kDELTA, "PRE-OPEN1");
   pk2.Add<f9set::ItchMsgHead>('x'); // 不認識的訊息.
   fon9_CheckTestResult("pk2.Ok", tb.Feed(pk2.Finish()) == f9set::MoldUdp64Result::Ok);
   fon9_CheckTestResult("pk2.NextSeqNo=8", tb.GetNextSeqNo() == 8 && tb.DupMsgCount_ == 6);
   fon9_CheckTestResult("UnknownMsg", tb.UnknownMsgCount_ == 1);

   // 缺號: 期望 8, 收到 10.
   MoldPkBuilder pk3{kSession, 10};
   char shortAdd[10] = {'A'};
   pk3.AddRaw(shortAdd, sizeof(shortAdd)); // 長度不足的 'A'.
   fon9_CheckTestResult("pk3.Ok", tb.Feed(pk3.Finish()) == f9set::MoldUdp64Result::Ok);
   fon9_CheckTestResult("Gap[8,10)", tb.GapCount_ == 1 && tb.GapMsgCount_ == 2
                        && tb.LastGapFrom_ == 8 && tb.LastGapTo_ == 10);
   fon9_CheckTestResult("ShortMsg", tb.ShortMsgCount_ == 1);

   // Heartbeat 也能偵測缺號: 期望 11, Heartbeat 說下一個是 12.
   MoldPkBuilder hb2{kSession, 12};
   fon9_CheckTestResult("Heartbeat.Gap", tb.Feed(hb2.Raw()) == f9set::MoldUdp64Result::Heartbeat
                        && tb.GapCount_ == 2 && tb.LastGapFrom_ == 11 && tb.GetNextSeqNo() == 12);

   // 封包有誤: Message Block 長度超過封包.
   MoldPkBuilder bad{kSession, 12};
   bad.Seconds(1792029601);
   std::string badpk = bad.Finish();
   badpk.resize(badpk.size() - 1);
   fon9_CheckTestResult("BadPacket.Truncated", tb.Feed(badpk) == f9set::MoldUdp64Result::BadPacket);
   fon9_CheckTestResult("BadPacket.TooShort", tb.FeedPacket(badpk.data(), 10) == f9set::MoldUdp64Result::BadPacket);
   fon9_CheckTestResult("BadPacket.NotConsumed", tb.BadPkCount_ == 2 && tb.GetNextSeqNo() == 12);
}

//--------------------------------------------------------------------------//
void TestOrderBook(TestBuilder& tb) {
   std::cout << "--- OrderBook ---" << std::endl;
   const f9set::ExgMdOrderBook& ptt = *tb.GetOrderBook(kPTT);
   MoldPkBuilder pk{kSession, tb.GetNextSeqNo()};
   // 買: 7 檔, 34.00 有 2 筆委託.
   pk.AddOrder(kPTT, 'B', 1, 3400, 1000);
   pk.AddOrder(kPTT, 'B', 2, 3400, 500);
   pk.AddOrder(kPTT, 'B', 3, 3375, 200);
   pk.AddOrder(kPTT, 'B', 4, 3350, 300);
   pk.AddOrder(kPTT, 'B', 5, 3325, 400);
   pk.AddOrder(kPTT, 'B', 6, 3300, 500);
   pk.AddOrder(kPTT, 'B', 7, 3275, 600);
   pk.AddOrderMpid(kPTT, 'B', 8, 3250, 700, "0001");
   // 賣: 6 檔; 賣方 OrderId 與買方重複(ITCH Order ID 只在 OrderBook + Side 之內唯一).
   pk.AddOrder(kPTT, 'S', 1, 3425, 100);
   pk.AddOrder(kPTT, 'S', 2, 3450, 200);
   pk.AddOrder(kPTT, 'S', 3, 3475, 300);
   pk.AddOrder(kPTT, 'S', 4, 3500, 400);
   pk.AddOrder(kPTT, 'S', 5, 3525, 500);
   pk.AddOrder(kPTT, 'S', 6, 3550, 600);
   pk.AddOrder(kPTT, 'S', 7, 3425, 50);
   // 隱藏數量委託: 不影響五檔.
   pk.AddOrder(kPTT, 'S', 8, 3400, 0);
   fon9_CheckTestResult("pk.Ok", tb.Feed(pk.Finish()) == f9set::MoldUdp64Result::Ok);
   fon9_CheckTestResult("OrderCount", ptt.GetOrderCount(f9set::ExgSide::Buy) == 8
                        && ptt.GetOrderCount(f9set::ExgSide::Sell) == 8);
   fon9_CheckTestResult("LevelCount", ptt.GetLevelCount(f9set::ExgSide::Buy) == 7
                        && ptt.GetLevelCount(f9set::ExgSide::Sell) == 6);
   fon9_CheckTestResult("BookChanged", tb.BookChangedCount_ == 16);

   fon9::fmkt::SymbBSData bs;
   ptt.GetBS(bs);
   fon9_CheckTestResult("BS.Buys",
                        IsPQ(bs.Buys_[0], 3400, 1500) && IsPQ(bs.Buys_[1], 3375, 200) && IsPQ(bs.Buys_[2], 3350, 300)
                        && IsPQ(bs.Buys_[3], 3325, 400) && IsPQ(bs.Buys_[4], 3300, 500));
   fon9_CheckTestResult("BS.Sells",
                        IsPQ(bs.Sells_[0], 3425, 150) && IsPQ(bs.Sells_[1], 3450, 200) && IsPQ(bs.Sells_[2], 3475, 300)
                        && IsPQ(bs.Sells_[3], 3500, 400) && IsPQ(bs.Sells_[4], 3525, 500));
   fon9_CheckTestResult("ToPri", ptt.ToPri(3425).To<double>() == 34.25);

   // SET 新系統為十檔.
   fon9::fmkt::PriQty pq10[10];
   fon9_CheckTestResult("Levels10.Buy", ptt.GetLevels(f9set::ExgSide::Buy, pq10, 10) == 7
                        && IsPQ(pq10[6], 3250, 700) && IsPQ(pq10[7], 0, 0));

   std::cout << "--- Deal ---" << std::endl;
   MoldPkBuilder pkd{kSession, tb.GetNextSeqNo()};
   pkd.State(kPTT, "OPEN1", 1000);
   pkd.Executed(kPTT, 'S', 1, 60, 9001, 2000);     // 部分成交.
   pkd.Executed(kPTT, 'S', 1, 40, 9002, 3000);     // 完全成交: 不會有 'D'.
   pkd.Executed(kPTT, 'S', 99, 10, 9003, 4000);    // 找不到委託.
   fon9_CheckTestResult("pkd.Ok", tb.Feed(pkd.Finish()) == f9set::MoldUdp64Result::Ok);
   fon9_CheckTestResult("Phase.Open", ptt.Phase_ == f9set::SetTradingPhase::Open && ptt.StateName_ == "OPEN1");
   fon9_CheckTestResult("Deals", tb.Deals_.size() == 2 && ptt.DealCount_ == 2 && ptt.TotalQty_ == 100);
   fon9_CheckTestResult("LastDeal", ptt.LastDeal_.Price_ == 3425 && ptt.LastDeal_.Qty_ == 40
                        && ptt.LastDeal_.MatchNo_ == 9002 && !ptt.LastDeal_.IsAuction_);
   fon9_CheckTestResult("LastDeal.Time", ptt.LastDeal_.Time_.ToEpochSeconds() == 1792029600);
   fon9_CheckTestResult("OrderNotFound", tb.OrderNotFoundCount_ == 1);
   ptt.GetBS(bs);
   fon9_CheckTestResult("Exec.BS.Sells", IsPQ(bs.Sells_[0], 3425, 50) && IsPQ(bs.Sells_[1], 3450, 200));
   fon9_CheckTestResult("Exec.OrderRemoved", ptt.GetOrderCount(f9set::ExgSide::Sell) == 7);

   std::cout << "--- Replace / Delete ---" << std::endl;
   MoldPkBuilder pkr{kSession, tb.GetNextSeqNo()};
   pkr.Replace(kPTT, 'B', 2, 3375, 800);  // 34.00 x 500 => 33.75 x 800.
   pkr.Delete(kPTT, 'B', 1);              // 34.00 價位消失.
   pkr.Delete(kPTT, 'S', 8);              // 隱藏數量委託.
   pkr.Delete(kPTT, 'S', 99);             // 找不到委託.
   fon9_CheckTestResult("pkr.Ok", tb.Feed(pkr.Finish()) == f9set::MoldUdp64Result::Ok);
   ptt.GetBS(bs);
   fon9_CheckTestResult("Replace.BS.Buys",
                        IsPQ(bs.Buys_[0], 3375, 1000) && IsPQ(bs.Buys_[1], 3350, 300) && IsPQ(bs.Buys_[4], 3275, 600));
   fon9_CheckTestResult("Delete.OrderCount", ptt.GetOrderCount(f9set::ExgSide::Buy) == 7
                        && ptt.GetOrderCount(f9set::ExgSide::Sell) == 6 && tb.OrderNotFoundCount_ == 2);

   std::cout << "--- Auction / Trade / Phase ---" << std::endl;
   MoldPkBuilder pka{kSession, tb.GetNextSeqNo()};
   pka.State(kPTT, "PRE-CLOSE");
   pka.Equilibrium(kPTT, 3400, 2000, 1500);
   // 集合競價: 買賣雙方都會有 'C', 只有 Printable=Y 的計入成交.
   pka.ExecutedWithPrice(kPTT, 'B', 2, 50, 9100, 3400, 'Y', 'Y');
   pka.ExecutedWithPrice(kPTT, 'S', 7, 50, 9100, 3400, 'Y', 'N');
   pka.Trade(kPTT, 30, 3410, 9200, 'Y', 'N');
   pka.Trade(kPTT, 30, 3410, 9201, 'N', 'N');
   pka.State(kPTT, "SOMETHING_NEW");
   fon9_CheckTestResult("pka.Ok", tb.Feed(pka.Finish()) == f9set::MoldUdp64Result::Ok);
   fon9_CheckTestResult("Equilibrium", ptt.EqPrice_ == 3400 && ptt.EqBidQty_ == 2000 && ptt.EqAskQty_ == 1500);
   fon9_CheckTestResult("Auction.Deal", tb.Deals_.size() == 4 && tb.Deals_[2].IsAuction_
                        && tb.Deals_[2].Price_ == 3400 && tb.Deals_[2].Qty_ == 50);
   fon9_CheckTestResult("Trade.Deal", tb.Deals_[3].Price_ == 3410 && tb.Deals_[3].Qty_ == 30
                        && ptt.TotalQty_ == 180 && ptt.DealCount_ == 4);
   ptt.GetBS(bs);
   fon9_CheckTestResult("Auction.BS", IsPQ(bs.Buys_[0], 3375, 950) && ptt.GetOrderCount(f9set::ExgSide::Sell) == 5);
   fon9_CheckTestResult("Phase.Unknown", ptt.Phase_ == f9set::SetTradingPhase::Unknown
                        && ptt.StateName_ == "SOMETHING_NEW");
}

//--------------------------------------------------------------------------//
void TestSession(TestBuilder& tb) {
   std::cout << "--- Session ---" << std::endl;
   MoldPkBuilder eos{kSession, tb.GetNextSeqNo(), f9set::kMoldUdp64EndOfSession};
   fon9_CheckTestResult("EndOfSession", tb.Feed(eos.Raw()) == f9set::MoldUdp64Result::EndOfSession
                        && tb.IsEndOfSession() && tb.EndOfSessionCount_ == 1);
   fon9_CheckTestResult("EndOfSession.Repeat", tb.Feed(eos.Raw()) == f9set::MoldUdp64Result::EndOfSession
                        && tb.EndOfSessionCount_ == 1);

   // 換日: 新的 Session 從序號 1 開始.
   MoldPkBuilder pk{"0000000002", 1};
   pk.Seconds(1792116000);
   pk.SystemEvent('O');
   fon9_CheckTestResult("NewSession", tb.Feed(pk.Finish()) == f9set::MoldUdp64Result::Ok
                        && tb.SessionChangedCount_ == 1 && !tb.IsEndOfSession() && tb.GetNextSeqNo() == 3);
   const f9set::ExgMdOrderBook& ptt = *tb.GetOrderBook(kPTT);
   fon9_CheckTestResult("ClearDaily", tb.StartOfMessagesCount_ == 2
                        && ptt.GetOrderCount(f9set::ExgSide::Buy) == 0 && ptt.GetLevelCount(f9set::ExgSide::Sell) == 0
                        && ptt.TotalQty_ == 0 && ptt.Phase_ == f9set::SetTradingPhase::Unknown
                        && ptt.Symbol_ == "PTT");
   fon9_CheckTestResult("PhaseMap", f9set::ItchStateNameToPhase(fon9::StrView{"INTERMISSION        "})
                        == f9set::SetTradingPhase::Intermission);
}

//--------------------------------------------------------------------------//
static bool IsRequest(const f9set::MoldUdp64Request& req, uint64_t seqNo, uint16_t count) {
   return req.SeqNo_.Get() == seqNo && req.MsgCount_.Get() == count && req.Session_ == fon9::StrView{kSession};
}
void TestRecover() {
   std::cout << "--- Recover ---" << std::endl;
   TestBuilder tb;
   tb.SetRecoverEnabled(true);
   MoldPkBuilder pk1{kSession, 1};
   pk1.Seconds(1792029600);
   pk1.Directory(kPTT, "PTT", 2, 100);
   fon9_CheckTestResult("pk1", tb.Feed(pk1.Finish()) == f9set::MoldUdp64Result::Ok && tb.GetNextSeqNo() == 3);

   // 缺 [3,5): 5,6 先暫存.
   MoldPkBuilder pk5{kSession, 5};
   pk5.AddOrder(kPTT, 'B', 5, 3400, 500);
   pk5.AddOrder(kPTT, 'B', 6, 3400, 600);
   fon9_CheckTestResult("Gap.Pending", tb.Feed(pk5.Finish()) == f9set::MoldUdp64Result::Pending
                        && tb.GetPendingCount() == 2 && tb.HasGap() && tb.GetGapEndSeqNo() == 5
                        && tb.LastGapFrom_ == 3 && tb.LastGapTo_ == 5 && tb.GapCount_ == 0
                        && tb.GetNextSeqNo() == 3);
   const f9set::ExgMdOrderBook& ptt = *tb.GetOrderBook(kPTT);
   fon9_CheckTestResult("Gap.NotApplied", ptt.GetOrderCount(f9set::ExgSide::Buy) == 0);

   const fon9::TimeStamp      now = fon9::UtcNow();
   const f9set::MoldUdp64RecoverArgs& args = tb.RecoverArgs_;
   // GapWait 之內不送出(等另一路補上).
   fon9_CheckTestResult("Recover.GapWait", tb.CheckRecover(now - fon9::TimeInterval_Second(1)) > fon9::TimeInterval{}
                        && tb.Requests_.empty());
   fon9_CheckTestResult("Recover.Request", tb.CheckRecover(now + args.GapWait_) == args.RetryInterval_
                        && tb.Requests_.size() == 1 && IsRequest(tb.Requests_[0], 3, 2) && tb.RequestCount_ == 1);
   fon9_CheckTestResult("Recover.WaitReply", tb.CheckRecover(now + args.GapWait_ + fon9::TimeInterval_Millisecond(100))
                        == args.RetryInterval_ - fon9::TimeInterval_Millisecond(100)
                        && tb.Requests_.size() == 1);

   // 回補封包: 補上 [3,5), 接著處理暫存的 5,6.
   MoldPkBuilder pk3{kSession, 3};
   pk3.AddOrder(kPTT, 'B', 3, 3375, 300);
   pk3.AddOrder(kPTT, 'B', 4, 3375, 400);
   fon9_CheckTestResult("Recover.Filled", tb.Feed(pk3.Finish()) == f9set::MoldUdp64Result::Ok
                        && tb.GetNextSeqNo() == 7 && !tb.HasGap() && tb.GetPendingCount() == 0
                        && ptt.GetOrderCount(f9set::ExgSide::Buy) == 4 && tb.GapCount_ == 0);
   fon9_CheckTestResult("Recover.NoGap", tb.CheckRecover(now) == fon9::TimeInterval{});
   // 再收到一次暫存過的封包: 重複.
   fon9_CheckTestResult("Recover.Dup", tb.Feed(pk5.Finish()) == f9set::MoldUdp64Result::Duplicate);

   // 另一路(A/B) 在送出回補要求之前補上.
   MoldPkBuilder pk8{kSession, 8};
   pk8.Delete(kPTT, 'B', 3);
   fon9_CheckTestResult("AB.Gap", tb.Feed(pk8.Finish()) == f9set::MoldUdp64Result::Pending && tb.HasGap());
   MoldPkBuilder pk7{kSession, 7};
   pk7.Delete(kPTT, 'B', 4);
   pk7.Delete(kPTT, 'B', 3);
   fon9_CheckTestResult("AB.Filled", tb.Feed(pk7.Finish()) == f9set::MoldUdp64Result::Ok
                        && tb.GetNextSeqNo() == 9 && tb.DupMsgCount_ == 3 && tb.Requests_.size() == 1
                        && ptt.GetOrderCount(f9set::ExgSide::Buy) == 2 && tb.OrderNotFoundCount_ == 0);

   // 回補失敗: 重送 MaxRetry 次之後放棄.
   MoldPkBuilder pk11{kSession, 11};
   pk11.AddOrder(kPTT, 'S', 11, 3500, 100);
   fon9_CheckTestResult("Retry.Gap", tb.Feed(pk11.Finish()) == f9set::MoldUdp64Result::Pending);
   fon9::TimeStamp t = fon9::UtcNow() + args.GapWait_;
   tb.Requests_.clear();
   for (unsigned L = 0; L < args.MaxRetry_; ++L) {
      tb.CheckRecover(t);
      t += args.RetryInterval_;
   }
   fon9_CheckTestResult("Retry.Requests", tb.Requests_.size() == args.MaxRetry_
                        && IsRequest(tb.Requests_.back(), 9, 2) && tb.HasGap());
   fon9_CheckTestResult("Retry.GiveUp", tb.CheckRecover(t) == fon9::TimeInterval{}
                        && tb.Requests_.size() == args.MaxRetry_
                        && tb.GapCount_ == 1 && tb.GapMsgCount_ == 2
                        && tb.LastLostFrom_ == 9 && tb.LastLostTo_ == 11
                        && tb.GetNextSeqNo() == 12 && ptt.GetOrderCount(f9set::ExgSide::Sell) == 1);

   // 無法送出回補要求: 直接放棄.
   tb.CanSendRequest_ = false;
   MoldPkBuilder hb{kSession, 15};
   fon9_CheckTestResult("SendFail.Gap", tb.Feed(hb.Raw()) == f9set::MoldUdp64Result::Heartbeat
                        && tb.HasGap() && tb.GetGapEndSeqNo() == 15);
   fon9_CheckTestResult("SendFail.Skip", tb.CheckRecover(fon9::UtcNow() + args.GapWait_) == fon9::TimeInterval{}
                        && tb.GapCount_ == 2 && tb.GetNextSeqNo() == 15 && !tb.HasGap());

   // 一次最多要求 MaxRequestCount_ 筆.
   tb.CanSendRequest_ = true;
   tb.Requests_.clear();
   MoldPkBuilder hb2{kSession, 15 + 5000};
   tb.Feed(hb2.Raw());
   tb.CheckRecover(fon9::UtcNow() + args.GapWait_);
   fon9_CheckTestResult("MaxRequestCount", tb.Requests_.size() == 1
                        && IsRequest(tb.Requests_[0], 15, args.MaxRequestCount_));
   tb.SkipGap();
   fon9_CheckTestResult("SkipGap", !tb.HasGap() && tb.GetNextSeqNo() == 15 + 5000 && tb.GapCount_ == 3);
}

//--------------------------------------------------------------------------//
struct TestGlimpse : public f9set::SoupBinTcpClient {
   fon9_NON_COPY_NON_MOVE(TestGlimpse);
   f9set::ExgMdItchParser& Parser_;
   unsigned LoginAccepted_{0};
   char     RejectReason_{0};
   unsigned HbCount_{0};
   unsigned EndOfSession_{0};
   unsigned Unknown_{0};
   std::string Debug_;
   TestGlimpse(f9set::ExgMdItchParser& parser) : Parser_(parser) {
   }
   void OnSoupLoginAccepted() override { ++this->LoginAccepted_; }
   void OnSoupLoginRejected(char reason) override { this->RejectReason_ = reason; }
   void OnSoupServerHeartbeat() override { ++this->HbCount_; }
   void OnSoupEndOfSession() override { ++this->EndOfSession_; }
   void OnSoupDebug(fon9::StrView text) override { this->Debug_ = text.ToString(); }
   void OnSoupUnknownPacket(char, const void*, unsigned) override { ++this->Unknown_; }
   void OnSoupSequencedData(uint64_t seqNo, const void* msg, unsigned msgsz) override {
      this->Parser_.ParseItchMessage(seqNo, msg, msgsz);
   }
};
void TestSoupBinTcp() {
   std::cout << "--- SoupBinTCP ---" << std::endl;
   const std::string login = f9set::SoupBinTcpClient::MakeLoginRequest("user01", "pass", "", 1);
   fon9_CheckTestResult("LoginRequest", login.size() == 3 + 46 && login[0] == 0 && login[1] == 47 && login[2] == 'L'
                        && login.compare(3, 6, "user01") == 0
                        && login.compare(9, 10, "pass      ") == 0
                        && login.compare(19, 10, "          ") == 0
                        && login.compare(29, 20, "                   1") == 0);
   const std::string hb = f9set::SoupBinTcpClient::MakeClientHeartbeat();
   fon9_CheckTestResult("ClientHeartbeat", hb == std::string("\0\1R", 3));
   fon9_CheckTestResult("Logout", f9set::SoupBinTcpClient::MakeLogoutRequest() == std::string("\0\1O", 3));

   TestBuilder tb;
   TestGlimpse soup{tb};
   std::string stream = MakeSoup('A', "0000000001                   1", 30);
   stream.append(MakeSoup('H', nullptr, 0));
   stream.append(MakeSoup('+', "hello", 5));
   stream.append(MakeSoup('?', "x", 1));
   // 分段餵入: 每次 1 byte.
   size_t pos = 0;
   for (size_t L = 1; L <= stream.size(); ++L)
      pos += soup.FeedBytes(stream.data() + pos, L - pos);
   fon9_CheckTestResult("FeedBytes.Split", pos == stream.size() && soup.LoginAccepted_ == 1 && soup.IsLoggedIn()
                        && soup.GetSession() == "0000000001" && soup.GetNextSeqNo() == 1
                        && soup.HbCount_ == 1 && soup.Debug_ == "hello" && soup.Unknown_ == 1);

   fon9::DcQueueList rxbuf;
   std::string rej = MakeSoup('J', "A", 1);
   rxbuf.Append(rej.data(), rej.size() - 1);
   fon9_CheckTestResult("FeedBuffer.Partial", soup.FeedBuffer(rxbuf) && soup.RejectReason_ == 0
                        && rxbuf.CalcSize() == rej.size() - 1);
   rxbuf.Append(rej.data() + rej.size() - 1, 1);
   std::string eos = MakeSoup('Z', nullptr, 0);
   rxbuf.Append(eos.data(), eos.size());
   fon9_CheckTestResult("FeedBuffer.Complete", soup.FeedBuffer(rxbuf) && soup.RejectReason_ == 'A'
                        && soup.EndOfSession_ == 1 && rxbuf.empty());
   rxbuf.Append("\0\0", 2);
   fon9_CheckTestResult("FeedBuffer.Bad", !soup.FeedBuffer(rxbuf) && soup.IsBadPacket());
}

//--------------------------------------------------------------------------//
void TestSnapshot() {
   std::cout << "--- GLIMPSE Snapshot ---" << std::endl;
   TestBuilder tb;
   TestGlimpse glimpse{tb};
   // 盤中啟動: 先開始等待快照, 再開始收 multicast.
   tb.BeginSnapshot();
   fon9_CheckTestResult("Snapshot.Waiting", tb.IsWaitingSnapshot());
   MoldPkBuilder rt{kSession, 9};
   rt.AddOrder(kPTT, 'B', 2, 3375, 300);    // 9: 快照已包含.
   rt.AddOrder(kPTT, 'S', 200, 3500, 100);  // 10.
   rt.Delete(kPTT, 'B', 1);                 // 11: 刪除快照裡的委託.
   fon9_CheckTestResult("Snapshot.Pending", tb.Feed(rt.Finish()) == f9set::MoldUdp64Result::Pending
                        && tb.GetPendingCount() == 3 && tb.GetOrderBook(kPTT) == nullptr);

   // GLIMPSE: 基本資料, 盤別, 委託簿, 最後是 'G'(下一個即時序號=10).
   MoldPkBuilder snap{kSession, 1};
   snap.Seconds(1792033200);
   snap.Directory(kPTT, "PTT", 2, 100);
   snap.State(kPTT, "OPEN1");
   snap.AddOrder(kPTT, 'B', 1, 3400, 500);
   snap.AddOrder(kPTT, 'B', 2, 3375, 300);
   snap.AddOrder(kPTT, 'S', 3, 3425, 200);
   f9set::ItchEndOfSnapshot eos;
   eos.MsgType_ = 'G';
   eos.SeqNo_.AssignFrom(fon9::StrView{"                  10"});
   snap.AddRaw(&eos, sizeof(eos));
   std::string stream = MakeSoup('A', "0000000001                   1", 30);
   stream.append(MoldToSoup(snap.Finish()));
   fon9_CheckTestResult("Glimpse.Feed", glimpse.FeedBytes(stream.data(), stream.size()) == stream.size());
   const f9set::ExgMdOrderBook* ptt = tb.GetOrderBook(kPTT);
   fon9_CheckTestResult("Snapshot.Done", ptt && !tb.IsWaitingSnapshot() && tb.SnapshotDoneCount_ == 1
                        && tb.GetNextSeqNo() == 12 && tb.GetPendingCount() == 0 && tb.DupMsgCount_ == 1
                        && tb.MsgTypeCount_[static_cast<unsigned char>('G')] == 1);
   fon9::fmkt::SymbBSData bs;
   ptt->GetBS(bs);
   fon9_CheckTestResult("Snapshot.Book", ptt->Phase_ == f9set::SetTradingPhase::Open
                        && ptt->GetOrderCount(f9set::ExgSide::Buy) == 1 && IsPQ(bs.Buys_[0], 3375, 300)
                        && IsPQ(bs.Sells_[0], 3425, 200) && IsPQ(bs.Sells_[1], 3500, 100)
                        && tb.OrderNotFoundCount_ == 0);

   // 快照中斷線: 放棄快照, 從暫存的第一個訊息開始處理.
   TestBuilder tb2;
   tb2.BeginSnapshot();
   MoldPkBuilder rt2{kSession, 100};
   rt2.Directory(kDELTA, "DELTA", 2, 100);
   rt2.AddOrder(kDELTA, 'B', 1, 10000, 100);
   tb2.Feed(rt2.Finish());
   tb2.AbortSnapshot();
   fon9_CheckTestResult("Snapshot.Abort", !tb2.IsWaitingSnapshot() && tb2.GetNextSeqNo() == 102
                        && tb2.GetOrderBook(kDELTA) && tb2.GetOrderBook(kDELTA)->GetOrderCount(f9set::ExgSide::Buy) == 1
                        && tb2.SnapshotDoneCount_ == 0);

   // 已在接收中, 重新取快照: 清除委託簿, 之後的即時訊息暫存.
   tb2.BeginSnapshot();
   fon9_CheckTestResult("Snapshot.Restart", tb2.GetOrderBook(kDELTA)->GetOrderCount(f9set::ExgSide::Buy) == 0
                        && tb2.GetOrderBook(kDELTA)->Symbol_ == "DELTA");
   MoldPkBuilder rt3{kSession, 102};
   rt3.AddOrder(kDELTA, 'B', 2, 10050, 100);
   tb2.Feed(rt3.Finish());
   fon9_CheckTestResult("Snapshot.Restart.Pending", tb2.GetPendingCount() == 1 && tb2.GetNextSeqNo() == 102);
   MoldPkBuilder snap2{kSession, 1};
   snap2.AddOrder(kDELTA, 'B', 1, 10000, 100);
   eos.SeqNo_.AssignFrom(fon9::StrView{"102"});
   snap2.AddRaw(&eos, sizeof(eos));
   const std::string snap2stream = MoldToSoup(snap2.Finish());
   TestGlimpse glimpse2{tb2};
   glimpse2.FeedBytes(snap2stream.data(), snap2stream.size());
   fon9_CheckTestResult("Snapshot.Restart.Done", tb2.GetNextSeqNo() == 103 && tb2.SnapshotDoneCount_ == 1
                        && tb2.GetOrderBook(kDELTA)->GetOrderCount(f9set::ExgSide::Buy) == 2);
}

//--------------------------------------------------------------------------//
void TestRtsBuilder() {
   std::cout << "--- MdRts ---" << std::endl;
   f9set::ExgMdSymbsSP     symbs{new f9set::ExgMdSymbs(std::string{}, true)};
   f9set::ExgMdRtsBuilder  rb{symbs};
   MoldPkBuilder pk1{kSession, 1};
   pk1.Seconds(1792029600); // 2026-10-15 02:00:00 UTC = 09:00:00 (UTC+7).
   pk1.SystemEvent('O');
   pk1.Directory(kPTT, "PTT", 2, 100);
   pk1.State(kPTT, "PRE-OPEN1", 1000);
   pk1.AddOrder(kPTT, 'B', 1, 3400, 500);
   pk1.AddOrder(kPTT, 'S', 1, 3425, 200);
   pk1.AddOrder(kPTT, 'S', 2, 3450, 300);
   rb.FeedPacket(pk1.Finish().data(), pk1.Finish().size());
   f9set::ExgMdSymb* symb = rb.GetSymb(kPTT);
   fon9_CheckTestResult("Symb", symb && ToStrView(symb->SymbId_) == "PTT" && symb->ShUnit_ == 100
                        && symb->OrderBookId_ == kPTT && symb->PriceDecimals_ == 2
                        && symb->TradingMarket_ == f9set::f9fmkt_TradingMarket_SET
                        && ToStrView(symb->NameUTF8_) == "PTT"
                        && symbs->GetSymb(fon9::StrView{"PTT"}).get() == symb);
   fon9_CheckTestResult("SessionSt.PreOpen", symb->TradingSessionSt_ == f9fmkt_TradingSessionSt_PreOpen
                        && symb->TradingSessionId_ == f9fmkt_TradingSessionId_Normal
                        && fon9::fmkt::IsMatchingMethod_AggregateAuction(symb->TwsFlags_)
                        && rb.PubSessionStCount_ == 1);
   // 尚未 FlushBS(): 五檔還沒更新.
   fon9_CheckTestResult("BS.BeforeFlush", symb->BS_.Data_.Buys_[0].Qty_ == 0 && rb.PubBSCount_ == 0);
   rb.FlushBS();
   const fon9::fmkt::SymbBSData& bs = symb->BS_.Data_;
   fon9_CheckTestResult("BS.Flush", rb.PubBSCount_ == 1
                        && bs.Buys_[0].Pri_ == Pri2(3400) && bs.Buys_[0].Qty_ == 500
                        && bs.Sells_[0].Pri_ == Pri2(3425) && bs.Sells_[1].Qty_ == 300
                        && bs.Buys_[1].Qty_ == 0 && bs.MarketSeq_ == 7
                        && bs.InfoTime_ == fon9::TimeInterval_Hour(9));
   rb.FlushBS();
   fon9_CheckTestResult("BS.NoChange", rb.PubBSCount_ == 1);

   MoldPkBuilder pk2{kSession, rb.GetNextSeqNo()};
   pk2.State(kPTT, "OPEN1", 2000);
   pk2.Executed(kPTT, 'S', 1, 50, 9001, 3000000);  // 34.25 x 50 @ 09:00:00.003
   pk2.Executed(kPTT, 'S', 1, 150, 9002, 3000000); // 34.25 x 150, 同一時間.
   pk2.AddOrder(kPTT, 'B', 9, 3300, 100);          // 第 6 檔以外不影響五檔? (買方只有 2 檔, 會影響)
   rb.FeedPacket(pk2.Finish().data(), pk2.Finish().size());
   rb.FlushBS();
   const fon9::fmkt::SymbDealData& deal = symb->Deal_.Data_;
   fon9_CheckTestResult("SessionSt.Open", symb->TradingSessionSt_ == f9fmkt_TradingSessionSt_Open
                        && fon9::fmkt::IsMatchingMethod_ContinuousMarket(symb->TwsFlags_)
                        && rb.PubSessionStCount_ == 2);
   fon9_CheckTestResult("Deal", rb.PubDealCount_ == 2 && deal.Deal_.Pri_ == Pri2(3425) && deal.Deal_.Qty_ == 150
                        && deal.TotalQty_ == 200 && deal.MarketSeq_ == 10
                        && deal.DealTime() == fon9::TimeInterval_Hour(9) + fon9::TimeInterval_Millisecond(3)
                        && !IsEnumContains(deal.Flags_, f9sv_DealFlag_DealTimeChanged)
                        && !IsEnumContains(deal.Flags_, f9sv_DealFlag_TotalQtyLost));
   fon9_CheckTestResult("OHL", symb->Open_.Data_.Pri_ == Pri2(3425) && symb->High_.Data_.Pri_ == Pri2(3425)
                        && symb->Low_.Data_.Pri_ == Pri2(3425));
   fon9_CheckTestResult("BS.AfterDeal", rb.PubBSCount_ == 2 && bs.Sells_[0].Pri_ == Pri2(3450)
                        && bs.Buys_[1].Pri_ == Pri2(3300) && bs.MarketSeq_ == 11);

   // 快照期間不發行五檔; 快照完成後, 只發行有變的五檔.
   rb.BeginSnapshot();
   MoldPkBuilder snap{kSession, 1};
   snap.AddOrder(kPTT, 'B', 1, 3400, 500);
   snap.AddOrder(kPTT, 'B', 9, 3300, 100);
   snap.AddOrder(kPTT, 'S', 2, 3450, 300);
   snap.Directory(kDELTA, "DELTA", 2, 100);
   snap.AddOrder(kDELTA, 'B', 1, 10000, 100);
   const std::string& snappk = snap.Finish();
   const fon9::byte*  pmsg = reinterpret_cast<const fon9::byte*>(snappk.data()) + sizeof(f9set::MoldUdp64Head);
   for (unsigned L = 0; L < 5; ++L) {
      const uint16_t msgsz = fon9::GetBigEndian<uint16_t>(pmsg);
      rb.ParseItchMessage(L + 1, pmsg + 2, msgsz);
      pmsg += 2 + msgsz;
      rb.FlushBS();
   }
   fon9_CheckTestResult("Snapshot.NoPublish", rb.PubBSCount_ == 2 && bs.Sells_[0].Pri_ == Pri2(3450));
   f9set::ItchEndOfSnapshot eos;
   eos.MsgType_ = 'G';
   eos.SeqNo_.AssignFrom(fon9::StrView{"12"});
   rb.ParseItchMessage(6, &eos, sizeof(eos));
   f9set::ExgMdSymb* delta = rb.GetSymb(kDELTA);
   fon9_CheckTestResult("Snapshot.Publish", rb.PubBSCount_ == 3 && delta
                        && delta->BS_.Data_.Buys_[0].Pri_ == Pri2(10000) && bs.Buys_[0].Qty_ == 500);

   // 新的一天: 委託簿清除, 五檔發行為空.
   MoldPkBuilder pk3{"0000000002", 1};
   pk3.SystemEvent('O');
   rb.FeedPacket(pk3.Finish().data(), pk3.Finish().size());
   rb.FlushBS();
   fon9_CheckTestResult("NewDay.BS", rb.PubBSCount_ == 5 && bs.Buys_[0].Qty_ == 0 && bs.Sells_[0].Qty_ == 0
                        && delta->BS_.Data_.Buys_[0].Qty_ == 0);
   fon9_CheckTestResult("PhaseMap.Rts",
                        f9set::ToTradingSessionSt(f9set::SetTradingPhase::Intermission) == f9fmkt_TradingSessionSt_Halted
                        && f9set::ToTradingSessionSt(f9set::SetTradingPhase::PreClose) == f9fmkt_TradingSessionSt_PreClose
                        && f9set::ToTradingSessionSt(f9set::SetTradingPhase::Unknown) == f9fmkt_TradingSessionSt_Unknown);
}

//--------------------------------------------------------------------------//
static int Replay(const char* fname) {
   FILE* fd = fopen(fname, "rb");
   if (!fd) {
      std::cout << "Open fail: " << fname << std::endl;
      return 3;
   }
   f9set::ExgMdBookBuilder builder;
   uint64_t pkCount = 0;
   fon9::byte lenbuf[2];
   std::vector<char> pk;
   while (fread(lenbuf, 1, 2, fd) == 2) {
      pk.resize(fon9::GetBigEndian<uint16_t>(lenbuf));
      if (fread(pk.data(), 1, pk.size(), fd) != pk.size())
         break;
      builder.FeedPacket(pk.data(), pk.size());
      ++pkCount;
   }
   fclose(fd);
   std::cout << "Packets=" << pkCount
             << "|BadPk=" << builder.BadPkCount_
             << "|Msgs=" << builder.MsgCount_
             << "|DupMsgs=" << builder.DupMsgCount_
             << "|Gaps=" << builder.GapCount_ << "/" << builder.GapMsgCount_
             << "|ShortMsgs=" << builder.ShortMsgCount_
             << "|UnknownMsgs=" << builder.UnknownMsgCount_
             << "|OrderNotFound=" << builder.OrderNotFoundCount_
             << "|OrderBooks=" << builder.GetOrderBookCount()
             << std::endl;
   for (unsigned L = 0; L < 256; ++L) {
      if (builder.MsgTypeCount_[L])
         std::cout << "  MsgType=" << static_cast<char>(L) << "|Count=" << builder.MsgTypeCount_[L] << std::endl;
   }
   return 0;
}

int main(int argc, char* argv[]) {
   const char* saveFile = nullptr;
   for (int L = 1; L < argc; ++L) {
      if (strcmp(argv[L], "--save") == 0 && L + 1 < argc)
         saveFile = argv[++L];
      else
         return Replay(argv[L]);
   }

   fon9::AutoPrintTestInfo utinfo{"f9set ExgMdItch"};
   TestBuilder tb;
   TestMoldUdp64(tb);
   TestOrderBook(tb);
   TestSession(tb);
   TestRecover();
   TestSoupBinTcp();
   TestSnapshot();
   TestRtsBuilder();

   if (saveFile) {
      FILE* fd = fopen(saveFile, "wb");
      for (const std::string& pk : tb.Packets_) {
         char lenbuf[2];
         fon9::PutBigEndian(lenbuf, static_cast<uint16_t>(pk.size()));
         fwrite(lenbuf, 1, 2, fd);
         fwrite(pk.data(), 1, pk.size(), fd);
      }
      fclose(fd);
      std::cout << "Saved: " << saveFile << "|Packets=" << tb.Packets_.size() << std::endl;
   }
}
