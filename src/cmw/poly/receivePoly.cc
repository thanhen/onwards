#include<cmw/Buffer.hh>
#include<boost/poly_collection/base_collection.hpp>
#include"testing.hh"
#include"zz.testing.hh"
#include<iostream>

int main ()
{
  using namespace ::cmw;
  try{
    BufferStack<SameFormat> buffer(udpServer("13579"));
    //buffer.sock_=udpServer("13579");

    for(;;){
      buffer.GetPacket();
      ::boost::base_collection<base> b;
      testing::Give(buffer,b);
      ::std::cout << "size is " <<b.size()<<::std::endl;
      ::std::cout << "size of der1is " <<b.size<derived1>()<<::std::endl;
      ::std::cout << "size of der3is " <<b.size<derived3>()<<::std::endl;
    }
    return 1;
  }catch(std::exception const& ex){
    ::std::cout << "failure: "<<ex.what()<<::std::endl;
  }
}
