// \file f9setouch/ExgOuchLineFactory.cpp
#include "f9setouch/ExgOuchLineFactory.hpp"

namespace f9setouch {

ExgOuchLineFactory::ExgOuchLineFactory(std::string logPathFmt, Named&& name)
   : baseFactory{std::move(name)}
   , basePathMaker{std::move(logPathFmt)} {
}
ExgOuchLineFactory::~ExgOuchLineFactory() {
}
fon9::io::SessionServerSP ExgOuchLineFactory::CreateSessionServer(fon9::IoManager& mgr, const fon9::IoConfigItem& cfg, std::string& errReason) {
   (void)mgr; (void)cfg;
   errReason = "f9setouch::ExgOuchLineFactory|err=Not support Server.";
   return fon9::io::SessionServerSP{};
}
fon9::io::SessionSP ExgOuchLineFactory::CreateSession(fon9::IoManager& mgr, const fon9::IoConfigItem& cfg, std::string& errReason) {
   ExgOuchLineMgr* lineMgr = dynamic_cast<ExgOuchLineMgr*>(&mgr);
   if (lineMgr == nullptr) {
      errReason = "IoManager must be f9setouch::ExgOuchLineMgr";
      return fon9::io::SessionSP{};
   }
   ExgOuchLineArgs args;
   errReason = ExgOuchLineArgsParser(args, ToStrView(cfg.SessionArgs_));
   if (!errReason.empty())
      return fon9::io::SessionSP{};
   std::string logFileName;
   if (args.IsNeedsLog_) {
      errReason = this->MakeLogPath(logFileName);
      if (!errReason.empty())
         return fon9::io::SessionSP{};
      logFileName.append("SetOuch_");
      logFileName.append(args.User_);
      logFileName.append(".log");
   }
   return this->CreateTradingLine(*lineMgr, args, std::move(logFileName), errReason);
}
fon9::io::SessionSP ExgOuchLineFactory::CreateTradingLine(ExgOuchLineMgr& lineMgr, const ExgOuchLineArgs& args,
                                                          std::string logFileName, std::string& errReason) {
   (void)errReason;
   return fon9::io::SessionSP{new ExgOuchTradingLine(lineMgr, args, std::move(logFileName))};
}

} // namespaces
