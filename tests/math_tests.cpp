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
 near(calculate(L"(-8)^(1/3)"),-2);near(calculate(L"(-8)^(2/3)"),4);
 near(calculate(L"(-8)^(-1/3)"),-.5);near(calculate(L"(-32)^(1/5)"),-2);
 near(calculate(L"(-8)^(2/6)"),-2);near(calculate(L"(-8)^3"),-512);
 for(auto expression:{L"(-8)^(1/2)",L"(-8)^(1/4)",L"(-8)^sqrt(2)",L"(-8)^pi",L"0^(-1/3)"})check(!math_query(expression).error.empty());
 auto cubeGraph=math_query(L"(x^2-16)^(1/3)",true);
 check(cubeGraph.error.empty());check(cubeGraph.points.size()==601);
 for(auto point:cubeGraph.points){check(std::isfinite(point.y));near(point.y,std::cbrt(point.x*point.x-16));}
 check(cubeGraph.ymin < -std::cbrt(16));
 check(cubeGraph.info.find(L"x-intercepts: -4  4  \n")!=std::wstring::npos);
 check(cubeGraph.info.find(L"turning points: (0, -2.51984209979)")!=std::wstring::npos);
 check(cubeGraph.info.find(L"y-intercept: -2.51984209979")!=std::wstring::npos);
 auto endpointRoots=math_query(L"x^2-16; -4; 4",true);
 check(endpointRoots.info.find(L"x-intercepts: -4  4  \n")!=std::wstring::npos);
 near(calculate(L"(2+3)*4"),20);near(calculate(L"sqrt(9)+ln(e)+log(8,2)"),7);
 near(calculate(L"sin(pi/2)+cos(0)"),2);near(calculate(L"1e-3"),.001);
 near(calculate(L"exp(0)"),1);near(calculate(L"x^2",3),9);
 near(calculate(L"2x",3),6);near(calculate(L"-2x^2",3),-18);
 near(calculate(L"2(x+1)",3),8);near(calculate(L"x(x+1)",3),12);
 near(calculate(L"(x+1)(x-1)",3),8);
 near(calculate(L"2sin(pi/2)"),2);near(calculate(L"2pi"),2*std::acos(-1));
 near(calculate(L"8/2(2+2)"),16); // Same precedence as explicit multiplication.
 near(calculate(L"(x^9 + x^2)/(2x-x^3)",2),-129);
 auto implicitGraph=math_query(L"(x^9 + x^2)/(2x-x^3)",true);
 auto explicitGraph=math_query(L"(x^9 + x^2)/(2*x-x^3)",true);
 check(implicitGraph.error.empty());check(implicitGraph.points.size()==601);
 check(implicitGraph.points.size()==explicitGraph.points.size());
 for(size_t i=0;i<implicitGraph.points.size();++i){
     auto a=implicitGraph.points[i],b=explicitGraph.points[i];
     near(a.x,b.x);
     if(std::isfinite(a.y))near(a.y,b.y);else check(!std::isfinite(b.y));
 }
 check(!std::isfinite(implicitGraph.points[300].y)); // Preserve the hole at x=0.
 near(std::stod(math_query(L"integral(2x,0,1)").value),1);
 for(auto expression:{L"1/0",L"sqrt(-1)",L"log(4,1)",L"log(4,-2)",L"1+",L"2foo",L"sin(1,2)",L"1e+",L"unknown(2)",L"1 2",L"2**3"})check(!math_query(expression).error.empty());
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
