/*
  @copyright Steve Keen 2019
  @author Russell Standish
  This file is part of Minsky.

  Minsky is free software: you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Minsky is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with Minsky.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef RESTPROCESS_H
#define RESTPROCESS_H
/// A classdesc descriptor to generate virtual xrap processing calls
#include <function.h>
#include <json_pack_base.h>

#include <map>

namespace classdesc
{
  /// interface for the REST processor
  class RESTProcessBase
  {
  public:
    virtual json_pack_t process(const string& remainder, const json_pack_t& arguments)=0;
    virtual json_pack_t signature() const=0;
    /// return signature for a function type F
    template <class F> json_pack_t functionSignature() const;
  };

  template <> inline string typeName<RESTProcessBase>() {return "RESTProcessBase";}
  
  template <class X, class Y>
  typename enable_if<is_convertible<X,Y>, void>::T
  convert(Y& y, const X& x)
  {y=x;}

  template <class X, class Y>
  typename enable_if<Not<is_convertible<X,Y>>, void>::T
  convert(Y& y, const X& x)
  {throw std::runtime_error(typeName<X>()+" connot be converted to "+typeName<Y>());}

  template <class X, class Y>
  void convert(const Y&, const X&) {}
  
  template <class X>
  typename enable_if<Not<is_sequence<X>>, void>::T
  convert(X& x, const json_pack_t& j)
  {
      switch (j.type())
        {
        case json_spirit::obj_type:
          j>>x;
          break;
        case json_spirit::array_type:
          {
            auto& arr=j.get_array();
            if (arr.size()>0)
              arr[0]>>x;
          }
          break;
        case json_spirit::str_type:
          convert(x,j.get_str());
          break;
        case json_spirit::bool_type:
          convert(x,j.get_bool());
          break;
        case json_spirit::int_type:
          convert(x,j.get_int());
          break;
        case json_spirit::real_type:
          convert(x,j.get_real());
          break;
        case json_spirit::null_type:
          break;
        }
  }
  
  template <class X>
  typename enable_if<is_sequence<X>, void>::T
  convert(X& x, const json_pack_t& j)
  {
    if (j.type()==json_spirit::array_type)
      {
        auto& arr=j.get_array();
        x.resize(arr.size());
        auto xi=x.begin();
        for (auto& ai: arr)
          ai >> *xi++;
      }
  }
  
  template <class X>
  void convert(const X* x, const json_pack_t& j)
  {}
  
  /// handle setting and getting of objects
  template <class T> class RESTProcessObject: public RESTProcessBase
  {
    T& obj;
  public:
    RESTProcessObject(T& obj): obj(obj) {}
    json_pack_t process(const string& remainder, const json_pack_t& arguments) override
    {
      json_pack_t r;
      if (remainder=="@type")
        return r<<typeName<T>();
      else if (remainder=="@signature")
        return signature();
      else
        convert(obj, arguments);
      return r<<obj;
    }
    json_pack_t signature() const override;
  };

  /// REST processor registry 
  struct RESTProcess_t: public std::map<std::string, std::unique_ptr<RESTProcessBase> >
  {
    /// ownership of \a rp is passed
    void add(string d, RESTProcessBase* rp)
    {
      std::replace(d.begin(),d.end(),'.','/');
      emplace(d, rp);
    }

    void process(const std::string& query, std::istream& input, std::ostream& output)
    {
      if (query[0]!='/') return;
      string cmd=query;
      
      for (auto cmdEnd=query.length(); cmdEnd>0;
           cmdEnd=cmd.rfind('/'), cmd=cmd.substr(0,cmdEnd))
        {
          auto r=find(cmd);
          if (r!=end())
            {
              auto tail=query.substr(cmdEnd);
              json_pack_t jin(json_spirit::mValue::null);
              if (input.peek()!='\n')
                read(input,jin);
              else
                {
                  string t;
                  getline(input,t); // absorb '\n'
                }
              if (tail=="/@signature")
                write(r->second->signature(), output);
              else
                write(r->second->process(tail, jin), output);
              output<<std::endl;
              break;
            }
        }
      if (cmd.empty())
        output << "Command not found"<<std::endl;
    }
  };
  
  template <class T>
  struct is_classdescGenerated:
    public And<is_class<T>, Not<is_container<T>>> {};
  
  /// descriptor for generating building REST processing registry
  template <class T>
  typename enable_if<is_classdescGenerated<T>, void>::T
  RESTProcess(RESTProcess_t&, string, T&);

  template <class T>
  typename enable_if<is_fundamental<T>, void>::T
  RESTProcess(RESTProcess_t& repo, const string& d, T& a)
  {repo.add(d, new RESTProcessObject<T>(a));}

  template <class T>
  void RESTProcess(RESTProcess_t& repo, const string& d, is_const_static, T& a)
  {RESTProcess(repo,d,a);}

  template <class T>
  void RESTProcess(RESTProcess_t& repo, const string& d, Exclude<T>& a)
  {}

  
  inline bool startsWith(const std::string& x, const std::string& prefix)
  {return x.size()>=prefix.size() && equal(prefix.begin(), prefix.end(), x.begin());}
  
  // sequences
  template <class T> class RESTProcessSequence: public RESTProcessBase
  {
    T& obj;
  public:
    RESTProcessSequence(T& obj): obj(obj) {}
    json_pack_t process(const string& remainder, const json_pack_t& arguments) override
    {
      json_pack_t r;
      // TODO @elem selector in remainder
      if (remainder.empty())
        convert(obj, arguments);
      else if (startsWith(remainder, "@elem/"))
        {
          auto r1=remainder.substr(6);
          auto n=r1.find('/');
          if (n!=string::npos)
            {
              size_t k=stoi(r1.substr(0,n));
              if (k<obj.size())
                {
                  auto elem=obj.begin();
                  advance(elem, k);
                  RESTProcess_t map;
                  RESTProcess(map,"",*elem);
                  auto i=map.find(r1.substr(n+1));
                  // TODO move splitter algorithm into RESTProcess_t
                  if (i!=map.end())
                    r<<i->second->process("",arguments);
                }
            }
        }
      else
        r<<obj;
      return r;
    }
    json_pack_t signature() const override;
  };

  template <class T>
  typename enable_if<is_sequence<T>, void>::T
  RESTProcess(RESTProcess_t& repo, const string& d, T& a)
  {repo.add(d, new RESTProcessSequence<T>(a));}

  template <class T> class RESTProcessAssociativeContainer: public RESTProcessBase
  {
    T& obj;
  public:
    RESTProcessAssociativeContainer(T& obj): obj(obj) {}
    json_pack_t process(const string& remainder, const json_pack_t& arguments) override
    {
      // TODO @elem selector in remainder
      if (remainder.empty())
        convert(obj, arguments);
      json_pack_t r;
      return r<<obj;
    }
    json_pack_t signature() const override;
  };

  template <class T>
  typename enable_if<is_associative_container<T>, void>::T
  RESTProcess(RESTProcess_t& repo, const string& d, T& a)
  {
    repo.add(d, new RESTProcessAssociativeContainer<T>(a));
  }

  template <class T>
  struct RESTProcessPtr: public RESTProcessBase
  {
    T& ptr;
    RESTProcessPtr(T& ptr): ptr(ptr) {}
    json_pack_t process(const string& remainder, const json_pack_t& arguments) override
    {
      if (ptr)
        return RESTProcessObject<typename T::element_type>(*ptr).process(remainder, arguments);
      else
        return {};
    }
    json_pack_t signature() const override;
  };

  template <class T>
  struct RESTProcessPtr<std::weak_ptr<T>>: public RESTProcessBase
  {
    std::weak_ptr<T>& ptr;
    RESTProcessPtr(std::weak_ptr<T>& ptr): ptr(ptr) {}
    json_pack_t process(const string& remainder, const json_pack_t& arguments) override
    {
      if (auto p=ptr.lock())
        return RESTProcessObject<T>(*p).process(remainder, arguments);
      else
        return {};
    }
    json_pack_t signature() const override;
  };

  

  
  
  // buffer adaptor for a vector of json_pack_t objects 
  class JSONBuffer
  {
    std::vector<json_pack_t> values;
    std::vector<json_pack_t>::iterator it;
  public:
    JSONBuffer(const json_pack_t& j) {
      if (j.type()==json_spirit::array_type)
        for (auto& i: j.get_array())
          values.push_back(i);
      else
        values.push_back(j);
      it=values.begin();
    }
    template <class T>
    JSONBuffer& operator>>(T& x) {
      if (it!=values.end()) (*it++) >> x;
      return *this;
    }
    template <class T>
    JSONBuffer& operator>>(const T& x) {++it; return *this;}
  };

  // member functions
  template <class F, class R=typename functional::Return<F>::T>
  class RESTProcessFunction: public RESTProcessBase
  {
    F f;
  public:
    RESTProcessFunction(F f): f(f) {}
    json_pack_t process(const string& remainder, const json_pack_t& arguments) override
    {
      functional::PackFunctor<JSONBuffer> argBuf(arguments);
      json_pack_t r;
      return r<<argBuf.call(f);
    }
    json_pack_t signature() const override {return functionSignature<F>();}
  };

  template <class F, class R>
  class RESTProcessFunction<F, std::unique_ptr<R>>: public RESTProcessBase
  {
    F f;
  public:
    RESTProcessFunction(F f): f(f) {}
    json_pack_t process(const string& remainder, const json_pack_t& arguments) override
    {
      throw std::runtime_error("currently unable to call functions returning unique_ptr");
    }
    json_pack_t signature() const override {return functionSignature<F>();}
  };

 
  
  template <class F>
  class RESTProcessFunction<F,void>: public RESTProcessBase
  {
    F f;
  public:
    RESTProcessFunction(F f): f(f) {}
    json_pack_t process(const string& remainder, const json_pack_t& arguments) override
    {
      functional::PackFunctor<JSONBuffer> argBuf(arguments);
      argBuf.call(f);
      return {};
    }
    json_pack_t signature() const override {return functionSignature<F>();}
  };

  template <class T, class F>
  typename enable_if<functional::is_member_function_ptr<F>, void>::T
  RESTProcess(RESTProcess_t& repo, const string& d, T& obj, F f)
  {
    typedef functional::bound_method<T,F> BM;
    repo.add(d, new RESTProcessFunction<BM>(BM(obj,f)));
  }

  template <class F>
  typename enable_if<functional::is_nonmember_function_ptr<F>, void>::T
  RESTProcess(RESTProcess_t& repo, const string& d, F f)
  {
    std::replace(d.begin(),d.end(),'.','/');
    repo.emplace(d, new RESTProcessFunction<F>(f));
  }

  inline void RESTProcess(RESTProcess_t& repo, const string& d, const char*& a)
  {repo.add(d,new RESTProcessObject<const char*>(a));}

  template <class E>
  typename enable_if<is_enum<E>, void>::T
  RESTProcess(RESTProcess_t& repo, const string& d, E& e)
  {
    // TODO
    //repo.add(d, new RESTProcessEnum<E>(e);
  }
  
}

namespace classdesc_access
{
  template <class T> struct access_RESTProcess;
}

using classdesc::RESTProcess;       
    
#include "use_mbr_pointers.h"
CLASSDESC_USE_OLDSTYLE_MEMBER_OBJECTS(RESTProcess);

#endif