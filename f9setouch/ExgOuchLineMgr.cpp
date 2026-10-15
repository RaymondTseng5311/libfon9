// \file f9setouch/ExgOuchLineMgr.cpp
#include "f9setouch/ExgOuchLineMgr.hpp"

namespace f9setouch {

ExgOuchLineMgr::~ExgOuchLineMgr() {
   this->OnBeforeDestroy();
}
void ExgOuchLineMgr::OnParentSeedClear() {
   this->OnBeforeDestroy();
   baseIo::OnParentSeedClear();
}
f9fmkt::SendRequestResult ExgOuchLineMgr::NoReadyLineReject(f9fmkt::TradingRequest& req, fon9::StrView cause) {
   if (this->RptHandler_)
      this->RptHandler_->OnOuchNoReadyLine(req, cause);
   return f9fmkt::SendRequestResult::NoReadyLine;
}

} // namespaces
