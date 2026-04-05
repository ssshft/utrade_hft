#pragma once

#include <string>
#include <map>
#include "include/Framework/base_strategy.h"
using namespace monster;
//把类名添加到map
#define ADD_CLASS_REGISTER(className) g_ReflectRegisterMap.insert(std::make_pair(#className, []() -> HFTStrategy_Base* { return new className; }))

//创建对象的函数指针
typedef HFTStrategy_Base* (*PF_CREATE_OBJECT)();

//类名创建对象
HFTStrategy_Base* ClassNameCreateObj(std::string _ClassName);

//初始化类名注册，
void InitClassReflect();
