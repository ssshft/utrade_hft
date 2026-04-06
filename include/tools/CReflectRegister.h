#pragma once

#include <string>
#include <map>
#include "base/base_strategy.h"

//把类名添加到map
#define ADD_CLASS_REGISTER(className) g_ReflectRegisterMap.insert(std::make_pair(#className, []() -> BaseStrategy* { return new className; }))

//创建对象的函数指针
typedef BaseStrategy* (*PF_CREATE_OBJECT)();

//类名创建对象
BaseStrategy* ClassNameCreateObj(std::string _ClassName);

//初始化类名注册，
void InitClassReflect();
