// \file f9set/ExgMdReceiverFactory.hpp
#ifndef __f9set_ExgMdReceiverFactory_hpp__
#define __f9set_ExgMdReceiverFactory_hpp__
#include "f9set/ExgMdSystem.hpp"
#include "fon9/framework/IoFactory.hpp"

namespace f9set {

/// 建立 SET 行情接收 Session, SessionArgs:
/// - Type=Mc       MoldUDP64 multicast(預設); Device=Dgram, DeviceArgs 例: "Group=233.54.12.1|Bind=26400|Interface=10.0.0.1";
/// - Type=Rr       MoldUDP64 Re-request;      Device=Dgram, DeviceArgs 例: "10.1.1.1:26500" (回補伺服器);
/// - Type=Glimpse  GLIMPSE 快照;              Device=TcpClient, 需要 User=, Pass=;
/// - PkLog=FileName  記錄收到的封包: 每個封包前加上 2 bytes(big-endian) 的長度,
///                   檔名前加上 MdSys.LogPathPre() + '_', 可用 f9setExgMdItch_UT 重播.
///                   (Mc, Rr 才有效)
class f9set_API ExgMdReceiverFactory : public fon9::SessionFactory {
   fon9_NON_COPY_NON_MOVE(ExgMdReceiverFactory);
   using base = fon9::SessionFactory;
public:
   const ExgMdSystemSP  MdSys_;

   template <class... ArgsT>
   ExgMdReceiverFactory(ExgMdSystemSP mdsys, ArgsT&&... args)
      : base(std::forward<ArgsT>(args)...)
      , MdSys_(std::move(mdsys)) {
   }

   fon9::io::SessionSP CreateSession(fon9::IoManager& ioMgr, const fon9::IoConfigItem& cfg, std::string& errReason) override;
   fon9::io::SessionServerSP CreateSessionServer(fon9::IoManager& ioMgr, const fon9::IoConfigItem& cfg, std::string& errReason) override;
};

} // namespaces
#endif//__f9set_ExgMdReceiverFactory_hpp__
