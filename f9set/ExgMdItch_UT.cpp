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
#include "fon9/TestTools.hpp"
#include <string>
#include <vector>

//--------------------------------------------------------------------------//
/// 產生 MoldUDP64 Downstream Packet.
class MoldPkBuilder {
   std::string Buf_;
   uint16_t    Count_{0};
   void* Alloc(size_t sz) {
      char lenbuf[2];
      fon9::PutBigEndian(lenbuf, static_cast<uint16_t>(sz));
      this->Buf_.append(lenbuf, 2);
      this->Buf_.append(sz, '\0');
      ++this->Count_;
      return &*(this->Buf_.end() - static_cast<std::ptrdiff_t>(sz));
   }
public:
   MoldPkBuilder(const char* session, uint64_t seqNo, uint16_t count = 0) {
      f9set::MoldUdp64Head head;
      memset(&head, 0, sizeof(head));
      head.Session_.AssignFrom(fon9::StrView_cstr(session));
      head.SeqNo_.Put(seqNo);
      head.MsgCount_.Put(count);
      this->Buf_.assign(reinterpret_cast<const char*>(&head), sizeof(head));
   }
   /// 加入一個訊息, 傳回的參考只能在下次 Add() 之前使用.
   template <class MsgT>
   MsgT& Add(char msgType, uint32_t nanos = 0) {
      MsgT* msg = static_cast<MsgT*>(this->Alloc(sizeof(MsgT)));
      memset(static_cast<void*>(msg), 0, sizeof(*msg));
      reinterpret_cast<char*>(msg)[0] = msgType;
      if (sizeof(MsgT) >= sizeof(f9set::ItchMsgHead))
         reinterpret_cast<f9set::ItchMsgHead*>(msg)->Nanoseconds_.Put(nanos);
      return *msg;
   }
   void AddRaw(const void* msg, size_t sz) {
      memcpy(this->Alloc(sz), msg, sz);
   }
   const std::string& Finish() {
      reinterpret_cast<f9set::MoldUdp64Head*>(&*this->Buf_.begin())->MsgCount_.Put(this->Count_);
      return this->Buf_;
   }
   /// Heartbeat 或 End of Session: 不要呼叫 Finish().
   const std::string& Raw() const {
      return this->Buf_;
   }

   // ----- 常用訊息.
   void Seconds(uint32_t secs) {
      this->Add<f9set::ItchSeconds>('T').Seconds_.Put(secs);
   }
   void SystemEvent(char code) {
      this->Add<f9set::ItchSystemEvent>('S').EventCode_ = code;
   }
   void Directory(uint32_t id, const char* symbol, uint16_t decimals, uint32_t roundLot) {
      auto& msg = this->Add<f9set::ItchOrderBookDirectory>('R');
      msg.OrderBookId_.Put(id);
      msg.Symbol_.AssignFrom(fon9::StrView_cstr(symbol));
      msg.LongName_.AssignFrom(fon9::StrView_cstr(symbol));
      msg.Isin_.AssignFrom(fon9::StrView{"TH0000000000"});
      msg.FinancialProduct_.Put(5);
      msg.TradingCurrency_.AssignFrom(fon9::StrView{"THB"});
      msg.PriceDecimals_.Put(decimals);
      msg.RoundLotSize_.Put(roundLot);
   }
   void State(uint32_t id, const char* stateName, uint32_t nanos = 0) {
      auto& msg = this->Add<f9set::ItchOrderBookState>('O', nanos);
      msg.OrderBookId_.Put(id);
      msg.StateName_.AssignFrom(fon9::StrView_cstr(stateName));
   }
   void AddOrder(uint32_t id, char side, uint64_t ordId, f9set::ItchPrice pri, uint64_t qty) {
      auto& msg = this->Add<f9set::ItchAddOrder>('A');
      msg.OrderBookId_.Put(id);
      msg.Side_ = side;
      msg.OrderId_.Put(ordId);
      msg.OrderBookPosition_.Put(1);
      msg.Quantity_.Put(qty);
      msg.Price_.Put(pri);
      msg.LotType_.Put(2);
   }
   void AddOrderMpid(uint32_t id, char side, uint64_t ordId, f9set::ItchPrice pri, uint64_t qty, const char* mpid) {
      auto& msg = this->Add<f9set::ItchAddOrderMpid>('F');
      msg.OrderBookId_.Put(id);
      msg.Side_ = side;
      msg.OrderId_.Put(ordId);
      msg.Quantity_.Put(qty);
      msg.Price_.Put(pri);
      msg.ParticipantId_.AssignFrom(fon9::StrView_cstr(mpid));
   }
   void Executed(uint32_t id, char side, uint64_t ordId, uint64_t qty, uint64_t matchNo, uint32_t nanos = 0) {
      auto& msg = this->Add<f9set::ItchOrderExecuted>('E', nanos);
      msg.OrderBookId_.Put(id);
      msg.Side_ = side;
      msg.OrderId_.Put(ordId);
      msg.ExecutedQuantity_.Put(qty);
      msg.MatchId_.MatchNo_.Put(matchNo);
   }
   void ExecutedWithPrice(uint32_t id, char side, uint64_t ordId, uint64_t qty, uint64_t matchNo,
                          f9set::ItchPrice pri, char cross, char printable) {
      auto& msg = this->Add<f9set::ItchOrderExecutedWithPrice>('C');
      msg.OrderBookId_.Put(id);
      msg.Side_ = side;
      msg.OrderId_.Put(ordId);
      msg.ExecutedQuantity_.Put(qty);
      msg.MatchId_.MatchNo_.Put(matchNo);
      msg.TradePrice_.Put(pri);
      msg.OccurredAtCross_ = cross;
      msg.Printable_ = printable;
   }
   void Replace(uint32_t id, char side, uint64_t ordId, f9set::ItchPrice pri, uint64_t qty) {
      auto& msg = this->Add<f9set::ItchOrderReplace>('U');
      msg.OrderBookId_.Put(id);
      msg.Side_ = side;
      msg.OrderId_.Put(ordId);
      msg.NewOrderBookPosition_.Put(1);
      msg.Quantity_.Put(qty);
      msg.Price_.Put(pri);
   }
   void Delete(uint32_t id, char side, uint64_t ordId) {
      auto& msg = this->Add<f9set::ItchOrderDelete>('D');
      msg.OrderBookId_.Put(id);
      msg.Side_ = side;
      msg.OrderId_.Put(ordId);
   }
   void Trade(uint32_t id, uint64_t qty, f9set::ItchPrice pri, uint64_t matchNo, char printable, char cross) {
      auto& msg = this->Add<f9set::ItchTrade>('P');
      msg.OrderBookId_.Put(id);
      msg.Side_ = ' ';
      msg.Quantity_.Put(qty);
      msg.TradePrice_.Put(pri);
      msg.MatchId_.MatchNo_.Put(matchNo);
      msg.Printable_ = printable;
      msg.OccurredAtCross_ = cross;
   }
   void Equilibrium(uint32_t id, f9set::ItchPrice pri, uint64_t bidQty, uint64_t askQty) {
      auto& msg = this->Add<f9set::ItchEquilibriumPriceUpdate>('Z');
      msg.OrderBookId_.Put(id);
      msg.EquilibriumPrice_.Put(pri);
      msg.BidQuantity_.Put(bidQty);
      msg.AskQuantity_.Put(askQty);
   }
};

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
   pk1.Seconds(1792040400); // 2026-10-15 09:00:00 (UTC+7).
   pk1.SystemEvent('O');
   pk1.Directory(kPTT, "PTT", 2, 100);
   pk1.Directory(kDELTA, "DELTA", 2, 100);
   pk1.State(kPTT, "PRE-OPEN1", 123456789);
   const std::string& p1 = pk1.Finish();
   fon9_CheckTestResult("pk1.Ok", tb.Feed(p1) == f9set::MoldUdp64Result::Ok);
   fon9_CheckTestResult("pk1.NextSeqNo=6", tb.GetNextSeqNo() == 6);
   fon9_CheckTestResult("pk1.MsgCount=5", tb.MsgCount_ == 5);
   fon9_CheckTestResult("Seconds", tb.GetItchSeconds() == 1792040400);
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
   bad.Seconds(1792040401);
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
   fon9_CheckTestResult("LastDeal.Time", ptt.LastDeal_.Time_.ToEpochSeconds() == 1792040400);
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
   pk.Seconds(1792126800);
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
