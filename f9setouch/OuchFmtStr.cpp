// \file f9setouch/OuchFmtStr.cpp
#include "f9setouch/OuchFmtStr.hpp"
#include "fon9/RevPrint.hpp"

namespace f9setouch {

static void RevPrintUnknown(fon9::RevBuffer& rbuf, const void* msg, unsigned msgsz) {
   fon9::RevPrint(rbuf, "?|type=", msgsz > 0 ? *static_cast<const char*>(msg) : ' ', "|sz=", msgsz);
}
template <class Msg>
static const Msg* CastMsg(const void* msg, unsigned msgsz) {
   return msgsz == sizeof(Msg) ? static_cast<const Msg*>(msg) : nullptr;
}

f9setouch_API void RevPrintOuchInbound(fon9::RevBuffer& rbuf, const void* msg, unsigned msgsz) {
   if (msgsz == 0)
      return RevPrintUnknown(rbuf, msg, msgsz);
   switch (static_cast<OuchMsgType>(*static_cast<const char*>(msg))) {
   case OuchMsgType::EnterOrder:
      if (auto m = CastMsg<OuchEnterOrder>(msg, msgsz)) {
         fon9::RevPrint(rbuf, "O|token=", m->OrderToken_, "|obid=", m->OrderBookId_.Get(),
                        "|side=", m->Side_, "|qty=", m->Quantity_.Get(), "|pri=", m->Price_.Get(),
                        "|tif=", m->TimeInForce_.Get(), "|type=", m->OrderType_,
                        "|acc=", m->ClientAccount_, "|ssqty=", m->ShortSellQuantity_.Get());
         return;
      }
      break;
   case OuchMsgType::ReplaceOrder:
      if (auto m = CastMsg<OuchReplaceOrder>(msg, msgsz)) {
         fon9::RevPrint(rbuf, "U|existing=", m->ExistingOrderToken_, "|token=", m->ReplacementOrderToken_,
                        "|qty=", m->Quantity_.Get(), "|pri=", m->Price_.Get(),
                        "|acc=", m->ClientAccount_, "|ssqty=", m->ShortSellQuantity_.Get());
         return;
      }
      break;
   case OuchMsgType::CancelOrder:
      if (auto m = CastMsg<OuchCancelOrder>(msg, msgsz)) {
         fon9::RevPrint(rbuf, "X|token=", m->OrderToken_);
         return;
      }
      break;
   case OuchMsgType::CancelByOrderId:
      if (auto m = CastMsg<OuchCancelByOrderId>(msg, msgsz)) {
         fon9::RevPrint(rbuf, "Y|obid=", m->OrderBookId_.Get(), "|side=", m->Side_, "|ordid=", m->OrderId_.Get());
         return;
      }
      break;
   default:
      break;
   }
   RevPrintUnknown(rbuf, msg, msgsz);
}

f9setouch_API void RevPrintOuchOutbound(fon9::RevBuffer& rbuf, const void* msg, unsigned msgsz) {
   if (msgsz == 0)
      return RevPrintUnknown(rbuf, msg, msgsz);
   switch (static_cast<OuchMsgType>(*static_cast<const char*>(msg))) {
   case OuchMsgType::OrderAccepted:
      if (auto m = CastMsg<OuchOrderAccepted>(msg, msgsz)) {
         fon9::RevPrint(rbuf, "A|token=", m->OrderToken_, "|obid=", m->OrderBookId_.Get(),
                        "|side=", m->Side_, "|ordid=", m->OrderId_.Get(),
                        "|qty=", m->Quantity_.Get(), "|pri=", m->Price_.Get(),
                        "|tif=", m->TimeInForce_.Get(), "|st=", m->OrderState_.Get(),
                        "|acc=", m->ClientAccount_, "|time=", m->Timestamp_.ToTimeStamp());
         return;
      }
      break;
   case OuchMsgType::OrderRejected:
      if (auto m = CastMsg<OuchOrderRejected>(msg, msgsz)) {
         fon9::RevPrint(rbuf, "J|token=", m->OrderToken_, "|code=", m->RejectCode_.Get(),
                        "|time=", m->Timestamp_.ToTimeStamp());
         return;
      }
      break;
   case OuchMsgType::OrderReplaced:
      if (auto m = CastMsg<OuchOrderReplaced>(msg, msgsz)) {
         fon9::RevPrint(rbuf, "U|token=", m->ReplacementOrderToken_, "|prev=", m->PreviousOrderToken_,
                        "|obid=", m->OrderBookId_.Get(), "|side=", m->Side_, "|ordid=", m->OrderId_.Get(),
                        "|qty=", m->Quantity_.Get(), "|pri=", m->Price_.Get(), "|st=", m->OrderState_.Get(),
                        "|time=", m->Timestamp_.ToTimeStamp());
         return;
      }
      break;
   case OuchMsgType::OrderCancelled:
      if (auto m = CastMsg<OuchOrderCancelled>(msg, msgsz)) {
         fon9::RevPrint(rbuf, "C|token=", m->OrderToken_, "|obid=", m->OrderBookId_.Get(),
                        "|side=", m->Side_, "|ordid=", m->OrderId_.Get(), "|reason=", m->Reason_.Get(),
                        "|time=", m->Timestamp_.ToTimeStamp());
         return;
      }
      break;
   case OuchMsgType::OrderExecuted:
      if (auto m = CastMsg<OuchOrderExecuted>(msg, msgsz)) {
         fon9::RevPrint(rbuf, "E|token=", m->OrderToken_, "|obid=", m->OrderBookId_.Get(),
                        "|qty=", m->TradedQuantity_.Get(), "|pri=", m->TradePrice_.Get(),
                        "|match=", m->MatchId_.MatchNo_.Get(), '.', m->MatchId_.ComboGroupId_.Get(),
                        "|src=", m->DealSource_.Get(), "|attr=", m->MatchAttributes_.Get(),
                        "|time=", m->Timestamp_.ToTimeStamp());
         return;
      }
      break;
   default:
      break;
   }
   RevPrintUnknown(rbuf, msg, msgsz);
}

} // namespaces
