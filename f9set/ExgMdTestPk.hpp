// \file f9set/ExgMdTestPk.hpp
//
// 單元測試用: 產生 MoldUDP64 封包、SoupBinTCP 封包.
//
#ifndef __f9set_ExgMdTestPk_hpp__
#define __f9set_ExgMdTestPk_hpp__
#include "f9set/ExgMdMoldUdp64.hpp"
#include "fon9/Endian.hpp"

fon9_BEFORE_INCLUDE_STD
#include <string>
fon9_AFTER_INCLUDE_STD

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
/// SoupBinTCP 封包: [Length][Type][Payload].
inline std::string MakeSoup(char pkType, const void* payload, size_t payloadsz) {
   std::string pk(3, pkType);
   fon9::PutBigEndian(&pk[0], static_cast<uint16_t>(payloadsz + 1));
   pk.append(static_cast<const char*>(payload), payloadsz);
   return pk;
}
/// 將 MoldPkBuilder 的訊息包成 SoupBinTCP Sequenced Data(GLIMPSE).
inline std::string MoldToSoup(const std::string& moldpk) {
   std::string       res;
   const fon9::byte* pmsg = reinterpret_cast<const fon9::byte*>(moldpk.data()) + sizeof(f9set::MoldUdp64Head);
   const fon9::byte* pend = reinterpret_cast<const fon9::byte*>(moldpk.data() + moldpk.size());
   while (pmsg < pend) {
      const uint16_t msgsz = fon9::GetBigEndian<uint16_t>(pmsg);
      res.append(MakeSoup('S', pmsg + 2, msgsz));
      pmsg += 2 + msgsz;
   }
   return res;
}

#endif//__f9set_ExgMdTestPk_hpp__
