#include<cmw/Buffer.hh>
#include"account.hh"
#include"messageIDs.hh"

#include<memory>//unique_ptr
#include<vector>
#include<assert.h>
#include<errno.h>
#include<stdint.h>
#include<stdio.h>
#include<string.h>
#include<sys/stat.h>
#include<time.h>
#include<unistd.h>//pread
#include<netinet/in.h>//sockaddr_in6,socklen_t
::int32_t previousTime;
using namespace ::cmw;

bool marshalFile (char const* name,SendBuffer& buf){
  struct ::stat sb;
  if(::stat(name,&sb)<0)raise("stat",name,errno);
  if(sb.st_mtime<=previousTime)return false;
  if('.'==name[0]||name[0]=='/')receive(buf,::strrchr(name,'/')+1);
  else receive(buf,name);
  insertNull(buf);

  fileWrapper fl(name,O_RDONLY);
  buf.receiveFile(fl.d,sb.st_size);
  return true;
}

struct cmwRequest{
  ::sockaddr_in6 frnt;
  ::socklen_t frntLn=sizeof frnt;
  MarshallingInt const acctNbr;
  FixedString120 path;
  ::int32_t now;
  char const* mdlFile;
  fileWrapper fl;

  cmwRequest (){}

  template<class R>
  explicit cmwRequest (ReceiveBuffer<R>& buf):acctNbr(buf),path(buf)
                     ,now(::time(nullptr)){
    char* const pos=::strrchr(path(),'/');
    if(nullptr==pos)raise("cmwRequest didn't find /");
    *pos='\0';
    setDirectory(path());
    mdlFile=pos+1;
    char last[60];
    ::snprintf(last,sizeof last,"%s.lastrun",mdlFile);
    ::new(&fl)fileWrapper(last,O_RDWR|O_CREAT,S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
    switch(::pread(fl.d,&previousTime,sizeof previousTime,0)){
      default:break;
      case 0:previousTime=0;break;
      case -1:raise("pread",errno);
    }
  }

  void marshal (SendBuffer& buf)const{
    acctNbr.marshal(buf);
    if(auto ind=buf.reserveBytes(1);
         !buf.receive(ind,marshalFile(mdlFile,buf))){
      receive(buf,mdlFile);
      insertNull(buf);
    }

    int8_t updatedFiles=0;
    auto const idx=buf.reserveBytes(sizeof updatedFiles);
    FILEwrapper f{mdlFile,"r"};
    while(auto line=f.fgets()){
      char const* tok=::strtok(line,"\n \r");
      if(!::strncmp(tok,"//",2)||!::strcmp(tok,"fixedMessageLengths"))continue;
      if(!::strcmp(tok,"--"))break;
      if(marshalFile(tok,buf))++updatedFiles;
    }
    buf.receive(idx,updatedFiles);
  }
};
#include"zz.mddlBck.hh"
using namespace ::mddlBck;

class cmwAmbassador{
  BufferCompressed<
#ifdef CMW_ENDIAN_BIG
    LeastSignificantFirst
#else
    SameFormat
#endif
  > cmwBuf;

  BufferStack<SameFormat> frntBuf;
  ::std::vector<cmwAccount> accounts;
  ::std::vector<::std::unique_ptr<cmwRequest>> pendingRequests;
  ::pollfd fds[2];
  int loginPause;

  void login (){
    marshal<messageID::login>(cmwBuf,accounts,cmwBuf.getSize());
    while(-1==(fds[0].fd=cmwBuf.sock_=connectWrapper("1.tcp.ngrok.io",
#ifdef CMW_ENDIAN_BIG
                      "28911"))){
#else
                      "26898"))){
#endif
      ::printf("connectWrapper %d\n",errno);
      pollWrapper(nullptr,0,loginPause);
    }

    while(!cmwBuf.flush());
    fds[0].events=POLLIN;
    while(!cmwBuf.gotPacket());
    if(!giveBool(cmwBuf))bail("Login:%s",giveStringView(cmwBuf).data());
    if(setNonblocking(fds[0].fd)==-1)bail("setNonb:%d",errno);
  }

  void reset (char const* context,char const* detail){
    syslogWrapper(LOG_ERR,"%s:%s",context,detail);
    frntBuf.reset();
    ::mddlFrnt::marshal<false>(frntBuf,{context," ",detail});
    for(auto& r:pendingRequests)
      if(r.get())frntBuf.send((::sockaddr*)&r->frnt,r->frntLn);
    pendingRequests.clear();
    closeSocket(fds[0].fd);
    cmwBuf.compressedReset();
    login();
  }

  bool sendData ()try{return cmwBuf.flush();}
  catch(::std::exception& e){reset("sendData",e.what());return true;}

  template<bool res,class...T>void outFront (cmwRequest const& req,T...t){
    frntBuf.reset();
    ::mddlFrnt::marshal<res>(frntBuf,{t...});
    frntBuf.send((::sockaddr*)&req.frnt,req.frntLn);
  }

public:
  cmwAmbassador (char*);
};

cmwAmbassador::cmwAmbassador (char* config):cmwBuf(1101000){
  FILEwrapper cfg{config,"r"};
  auto checkField=[&](char const* fld){
    if(::strcmp(fld,::strtok(cfg.fgets()," ")))bail("Expected %s",fld);
  };
  char const* tok;
  while((tok=::strtok(cfg.fgets()," "))&&!::strcmp("Account-number",tok)){
    auto num=fromChars(::strtok(nullptr,"\n \r"));
    checkField("Password");
    accounts.emplace_back(num,::strtok(nullptr,"\n \r"));
  }
  if(accounts.empty())bail("An account number is required.");
  if(::strcmp("UDP-port-number",tok))bail("Expected UDP-port-number");
  fds[1].fd=frntBuf.sock_=udpServer(::strtok(nullptr,"\n \r"));
  fds[1].events=POLLIN;

  checkField("Login-attempts-interval-in-milliseconds");
  loginPause=fromChars(::strtok(nullptr,"\n \r"));
  checkField("Keepalive-interval-in-milliseconds");
  int const keepaliveInterval=fromChars(::strtok(nullptr,"\n \r"));

  login();
  for(;;){
    if(0==pollWrapper(fds,2,keepaliveInterval)){
      if(pendingRequests.empty())
        try{
          marshal<messageID::keepalive>(cmwBuf);
          fds[0].events|=POLLOUT;
          pendingRequests.push_back(nullptr);
        }catch(::std::exception& e){reset("Keepalive",e.what());}
      else reset("Keepalive","No reply from CMW");
      continue;
    }

    try{
      if(fds[0].revents&POLLIN&&cmwBuf.gotPacket()){
        do{
          assert(!pendingRequests.empty());
          if(pendingRequests.front().get()){
            auto const& req=*pendingRequests.front();
            if(giveBool(cmwBuf)){
              Write(req.fl.d,&req.now,sizeof req.now);
              setDirectory(req.path.c_str());
              giveFiles(cmwBuf);
              outFront<true>(req);
            }else outFront<false>(req,"CMW:",giveStringView(cmwBuf));
          }
          pendingRequests.erase(::std::begin(pendingRequests));
        }while(cmwBuf.nextMessage());
      }
    }catch(fiasco& e){
      reset("Fiasco",e.what());
      continue;
    }catch(::std::exception& e){
      syslogWrapper(LOG_ERR,"Reply from CMW %s",e.what());
      assert(!pendingRequests.empty());
      if(pendingRequests.front().get())
        outFront<false>(*pendingRequests.front(),e.what());
      pendingRequests.erase(::std::begin(pendingRequests));
    }

    if(fds[0].revents&POLLOUT&&sendData())fds[0].events=POLLIN;

    if(fds[1].revents&POLLIN){
      bool gotAddr=false;
      cmwRequest* req=nullptr;
      try{
        req=&*pendingRequests.emplace_back(::std::make_unique<cmwRequest>());
        frntBuf.getPacket((::sockaddr*)&req->frnt,&req->frntLn);
        gotAddr=true;
        ::new(req)cmwRequest(frntBuf);
        marshal<messageID::generate>(cmwBuf,*req);
      }catch(::std::exception& e){
        syslogWrapper(LOG_ERR,"Accept request:%s",e.what());
        if(gotAddr)outFront<false>(*req,e.what());
        if(req)pendingRequests.pop_back();
        continue;
      }
      if(!sendData())fds[0].events|=POLLOUT;
    }
  }
}

int main (int ac,char** av)try{
  ::openlog(av[0],LOG_PID|LOG_NDELAY,LOG_USER);
  if(ac!=2)bail("Usage: cmwA config-file");
  cmwAmbassador{av[1]};
}catch(::std::exception& e){bail("Oops:%s",e.what());
}catch(...){bail("Unknown exception!");}
