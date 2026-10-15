// \file f9set/ExgTypes.cpp
#include "f9set/ExgMdItchFmt.hpp"
#include "fon9/StrTools.hpp"

namespace f9set {

fon9::StrView ToStrView(SetTradingPhase v) {
   switch (v) {
   case SetTradingPhase::Unknown:      return fon9::StrView{"Unknown"};
   case SetTradingPhase::PreTrading:   return fon9::StrView{"PreTrading"};
   case SetTradingPhase::PreOpen:      return fon9::StrView{"PreOpen"};
   case SetTradingPhase::Open:         return fon9::StrView{"Open"};
   case SetTradingPhase::Intermission: return fon9::StrView{"Intermission"};
   case SetTradingPhase::PreClose:     return fon9::StrView{"PreClose"};
   case SetTradingPhase::OffHour:      return fon9::StrView{"OffHour"};
   case SetTradingPhase::Closed:       return fon9::StrView{"Closed"};
   case SetTradingPhase::Pause:        return fon9::StrView{"Pause"};
   case SetTradingPhase::Halt:         return fon9::StrView{"Halt"};
   case SetTradingPhase::Suspend:      return fon9::StrView{"Suspend"};
   }
   return fon9::StrView{"?"};
}

SetTradingPhase ItchStateNameToPhase(fon9::StrView stateName) {
   fon9::StrTrimTail(&stateName);
   for (const ItchStateNameMap& m : kItchStateNameMap) {
      if (stateName == fon9::StrView_cstr(m.StateName_))
         return m.Phase_;
   }
   return SetTradingPhase::Unknown;
}

} // namespaces
