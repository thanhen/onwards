#pragma once
#include<stdint.h>
#include<stdlib.h> //strtol

namespace cmw{
class SendBuffer;
template<class R> class ReceiveBuffer;

class marshalling_integer{
  int32_t value;

public:
  inline marshalling_integer (){}
  inline explicit marshalling_integer (int32_t v):value(v){}
  inline explicit marshalling_integer (char const* v):value(::strtol(v,0,10)){}

  // Reads a sequence of bytes in variable-length format and
  // composes a 32 bit integer.
  template<class R>
  explicit marshalling_integer (ReceiveBuffer<R>& b):value(0){
    uint32_t shift=1;
    for(;;){
      uint8_t abyte=b.GiveOne();
      value+= (abyte&127)*shift;
      if((abyte&128)==0)return;
      shift<<=7;
      value+=shift;
    }
  }

  inline void operator= (int32_t rs){value=rs;}
  inline auto operator() ()const{return value;}
  inline bool operator== (marshalling_integer rs)const{return value==rs();}
  inline bool operator== (int32_t rs)const{return value==rs;}
  void Marshal (SendBuffer&,bool=false)const;
};
}
