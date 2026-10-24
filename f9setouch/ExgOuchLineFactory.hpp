// \file f9setouch/ExgOuchLineFactory.hpp
#ifndef __f9setouch_ExgOuchLineFactory_hpp__
#define __f9setouch_ExgOuchLineFactory_hpp__
#include "f9setouch/ExgOuchTradingLine.hpp"
#include "fon9/framework/IoFactory.hpp"

namespace f9setouch {

/// 建立 SET OUCH 下單線路的 Session Factory.
/// - IoManager 必須是 ExgOuchLineMgr;
/// - SessionArgs 請參考 ExgOuchLineArgs, 例: "User=U00001|Pass=xxxx|Fc=100/1000";
/// - Log 檔名: logPathFmt(例: "logs/{0:f+'L'}/") + "SetOuch_" + User + ".log";
/// - 券商端, 不支援 SessionServer.
class f9setouch_API ExgOuchLineFactory : public fon9::SessionFactory, public f9fmkt::TradingLineLogPathMaker {
   fon9_NON_COPY_NON_MOVE(ExgOuchLineFactory);
   using baseFactory = fon9::SessionFactory;
   using basePathMaker = f9fmkt::TradingLineLogPathMaker;

protected:
   /// 衍生者可覆寫, 建立自訂的 ExgOuchTradingLine(例如: 覆寫 OnOuchReport()).
   virtual fon9::io::SessionSP CreateTradingLine(ExgOuchLineMgr& lineMgr, const ExgOuchLineArgs& args,
                                                 std::string logFileName, std::string& errReason);

public:
   ExgOuchLineFactory(std::string logPathFmt, Named&& name);
   ~ExgOuchLineFactory();

   fon9::io::SessionSP CreateSession(fon9::IoManager& mgr, const fon9::IoConfigItem& cfg, std::string& errReason) override;
   fon9::io::SessionServerSP CreateSessionServer(fon9::IoManager& mgr, const fon9::IoConfigItem& cfg, std::string& errReason) override;
};

} // namespaces
#endif//__f9setouch_ExgOuchLineFactory_hpp__
