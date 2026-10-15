// \file f9set/ExgMdItchParser.cpp
#include "f9set/ExgMdItchParser.hpp"

namespace f9set {

ExgMdItchParser::ExgMdItchParser() {
   this->ClearStatus();
}
ExgMdItchParser::~ExgMdItchParser() {
}
void ExgMdItchParser::ClearStatus() {
   base::ClearStatus();
   memset(this->MsgTypeCount_, 0, sizeof(this->MsgTypeCount_));
   this->ShortMsgCount_ = 0;
   this->UnknownMsgCount_ = 0;
}

void ExgMdItchParser::OnMoldMessage(uint64_t seqNo, const void* msg, unsigned msgsz) {
   this->ParseItchMessage(seqNo, msg, msgsz);
}

template <class MsgT>
static inline const MsgT* CastItchMsg(const void* msg, unsigned msgsz) {
   return msgsz >= sizeof(MsgT) ? reinterpret_cast<const MsgT*>(msg) : nullptr;
}

void ExgMdItchParser::ParseItchMessage(uint64_t seqNo, const void* msg, unsigned msgsz) {
   if (fon9_UNLIKELY(msgsz <= 0)) {
      ++this->ShortMsgCount_;
      return;
   }
   this->CurSeqNo_ = seqNo;
   const uint8_t msgType = *reinterpret_cast<const uint8_t*>(msg);
   ++this->MsgTypeCount_[msgType];

   #define case_ITCH_MSG(type, MsgT)                              \
   case ItchMsgType::type:                                        \
      if (const MsgT* pmsg = CastItchMsg<MsgT>(msg, msgsz)) {     \
         this->OnItch##type(*pmsg);                               \
         return;                                                  \
      }                                                           \
      break;
   //------------------------------------
   switch (static_cast<ItchMsgType>(msgType)) {
   case ItchMsgType::Seconds:
      if (const ItchSeconds* pmsg = CastItchMsg<ItchSeconds>(msg, msgsz)) {
         this->ItchSeconds_ = pmsg->Seconds_.Get();
         return;
      }
      break;
   case ItchMsgType::AddOrder:
      if (const ItchAddOrder* pmsg = CastItchMsg<ItchAddOrder>(msg, msgsz)) {
         this->OnItchAddOrder(*pmsg, nullptr);
         return;
      }
      break;
   case ItchMsgType::AddOrderMpid:
      if (const ItchAddOrderMpid* pmsg = CastItchMsg<ItchAddOrderMpid>(msg, msgsz)) {
         this->OnItchAddOrder(*pmsg, &pmsg->ParticipantId_);
         return;
      }
      break;
   case_ITCH_MSG(SystemEvent,            ItchSystemEvent);
   case_ITCH_MSG(OrderBookDirectory,     ItchOrderBookDirectory);
   case_ITCH_MSG(TickSize,               ItchTickSize);
   case_ITCH_MSG(OrderBookState,         ItchOrderBookState);
   case_ITCH_MSG(OrderExecuted,          ItchOrderExecuted);
   case_ITCH_MSG(OrderExecutedWithPrice, ItchOrderExecutedWithPrice);
   case_ITCH_MSG(OrderReplace,           ItchOrderReplace);
   case_ITCH_MSG(OrderDelete,            ItchOrderDelete);
   case_ITCH_MSG(Trade,                  ItchTrade);
   case_ITCH_MSG(EquilibriumPriceUpdate, ItchEquilibriumPriceUpdate);
   default:
   case ItchMsgType::CombinationOrderBookDirectory: // 組合單目前不處理.
      ++this->UnknownMsgCount_;
      this->OnItchUnknown(seqNo, msg, msgsz);
      return;
   }
   #undef case_ITCH_MSG
   ++this->ShortMsgCount_;
}

void ExgMdItchParser::OnItchSystemEvent(const ItchSystemEvent&) {}
void ExgMdItchParser::OnItchOrderBookDirectory(const ItchOrderBookDirectory&) {}
void ExgMdItchParser::OnItchTickSize(const ItchTickSize&) {}
void ExgMdItchParser::OnItchOrderBookState(const ItchOrderBookState&) {}
void ExgMdItchParser::OnItchAddOrder(const ItchAddOrder&, const ItchAlpha<7>*) {}
void ExgMdItchParser::OnItchOrderExecuted(const ItchOrderExecuted&) {}
void ExgMdItchParser::OnItchOrderExecutedWithPrice(const ItchOrderExecutedWithPrice&) {}
void ExgMdItchParser::OnItchOrderReplace(const ItchOrderReplace&) {}
void ExgMdItchParser::OnItchOrderDelete(const ItchOrderDelete&) {}
void ExgMdItchParser::OnItchTrade(const ItchTrade&) {}
void ExgMdItchParser::OnItchEquilibriumPriceUpdate(const ItchEquilibriumPriceUpdate&) {}
void ExgMdItchParser::OnItchUnknown(uint64_t, const void*, unsigned) {}

} // namespaces
