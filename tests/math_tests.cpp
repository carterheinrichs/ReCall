#include "math_engine.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace recall;
void check(bool ok){if(!ok)throw std::runtime_error("math test failed");}
void near(double a,double b){check(std::abs(a-b)<1e-9);}
int main(){try{
 near(calculate(L"1+1"),2);near(calculate(L"2+3*4"),14);
 near(calculate(L"-2^2"),-4);near(calculate(L"2^3^2"),512);near(calculate(L"2^-2"),.25);
 near(calculate(L"(2+3)*4"),20);near(calculate(L"sqrt(9)+ln(e)+log(8,2)"),7);
 near(calculate(L"sin(pi/2)+cos(0)"),2);near(calculate(L"1e-3"),.001);
 near(calculate(L"exp(0)"),1);near(calculate(L"x^2",3),9);
 for(auto expression:{L"1/0",L"sqrt(-1)",L"log(4,1)",L"log(4,-2)",L"1+",L"2foo",L"sin(1,2)",L"1e+",L"unknown(2)"})check(!math_query(expression).error.empty());
 check(!math_query(L"exp(x)").error.empty());
 near(std::stod(math_query(L"integral(x^2,0,1)").value),1.0/3);
 near(std::stod(math_query(L"integral(sin(x),pi,0)").value),-2);
 check(!math_query(L"integral(1/x,-1,1)").error.empty());
 auto graph=math_query(L"x^2-4; -5; 5",true);check(graph.error.empty());check(graph.points.size()==601);check(graph.info.find(L"-2")!=std::wstring::npos);check(graph.info.find(L"(0, -4)")!=std::wstring::npos);
 check(!math_query(L"x; 5; -5",true).error.empty());check(!math_query(L"bogus(x)",true).error.empty());
 auto pole=math_query(L"1/x",true);check(pole.error.empty());check(pole.info.find(L"x-intercepts: none detected")!=std::wstring::npos);
 check(math_query(L"").value.empty());
 std::cout<<"Math tests passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
