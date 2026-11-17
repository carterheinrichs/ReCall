#include "math_engine.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <source_location>
using namespace recall;
void check(bool ok,std::source_location location=std::source_location::current()){
 if(!ok)throw std::runtime_error("math test failed at line "+std::to_string(location.line()));
}
void near(double a,double b,std::source_location location=std::source_location::current()){check(std::abs(a-b)<1e-9,location);}
int main(){try{
 near(calculate(L"1+1"),2);near(calculate(L"2+3*4"),14);
 near(calculate(L"-2^2"),-4);near(calculate(L"2^3^2"),512);near(calculate(L"2^-2"),.25);
 near(calculate(L"(-8)^(1/3)"),-2);near(calculate(L"(-8)^(2/3)"),4);
 near(calculate(L"(-8)^(-1/3)"),-.5);near(calculate(L"(-32)^(1/5)"),-2);
 near(calculate(L"(-8)^(2/6)"),-2);near(calculate(L"(-8)^3"),-512);
 for(auto expression:{L"(-8)^(1/2)",L"(-8)^(1/4)",L"(-8)^sqrt(2)",L"(-8)^pi",L"0^(-1/3)"})check(!math_query(expression).error.empty());
 auto cubeGraph=math_query(L"(x^2-16)^(1/3)",true);
 check(cubeGraph.error.empty());check(cubeGraph.points.size()>601);
 for(auto point:cubeGraph.points){if(std::isfinite(point.y))near(point.y,std::cbrt(point.x*point.x-16));}
 check(cubeGraph.ymin < -std::cbrt(16));
 check(cubeGraph.info.find(L"x-intercepts: -4  4  \n")!=std::wstring::npos);
 check(cubeGraph.turns.size()==1);check(std::abs(cubeGraph.turns[0].x)<1e-6);near(cubeGraph.turns[0].y,-std::cbrt(16));
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
 check(implicitGraph.error.empty());check(implicitGraph.points.size()>601);
 check(implicitGraph.points.size()==explicitGraph.points.size());
 for(size_t i=0;i<implicitGraph.points.size();++i){
     auto a=implicitGraph.points[i],b=explicitGraph.points[i];
     near(a.x,b.x);
     if(std::isfinite(a.y))near(a.y,b.y);else check(!std::isfinite(b.y));
 }
 bool hole=false;for(auto p:implicitGraph.points)if(p.x==0&&!std::isfinite(p.y))hole=true;check(hole);
 near(std::stod(math_query(L"integral(2x,0,1)").value),1);
 for(auto expression:{L"1/0",L"sqrt(-1)",L"log(4,1)",L"log(4,-2)",L"1+",L"2foo",L"sin(1,2)",L"1e+",L"unknown(2)",L"1 2",L"2**3"})check(!math_query(expression).error.empty());
 check(!math_query(L"exp(x)").error.empty());
 near(std::stod(math_query(L"integral(x^2,0,1)").value),1.0/3);
 near(std::stod(math_query(L"integral(sin(x),pi,0)").value),-2);
 check(!math_query(L"integral(1/x,-1,1)").error.empty());
 auto graph=math_query(L"x^2-4; -5; 5",true);check(graph.error.empty());check(graph.points.size()>=601);check(graph.info.find(L"-2")!=std::wstring::npos);check(graph.turns.size()==1);check(std::abs(graph.turns[0].x)<1e-6);near(graph.turns[0].y,-4);
 check(!math_query(L"x; 5; -5",true).error.empty());check(!math_query(L"bogus(x)",true).error.empty());
 auto pole=math_query(L"1/x",true);check(pole.error.empty());check(pole.info.find(L"x-intercepts: none detected")!=std::wstring::npos);
 // Tangent roots and refined extrema must agree with the known periodic peaks.
 auto logarithm=math_query(L"ln(sin(x))",true);check(logarithm.error.empty());
 check(logarithm.roots.size()==3);check(logarithm.turns.size()==3);
 double pi=std::acos(-1);
 for(size_t i=0;i<3;++i){double x=pi/2+(static_cast<int>(i)-1)*2*pi;check(std::abs(logarithm.roots[i].x-x)<1e-6);check(std::abs(logarithm.turns[i].x-x)<1e-6);}
 auto tangent=math_query(L"(x-0.12345)^2",true);check(tangent.roots.size()==1);check(std::abs(tangent.roots[0].x-.12345)<1e-6);
 check(math_query(L"(x-0.12345)^2+0.00001",true).roots.empty());
 auto cusp=math_query(L"abs(x-0.12345)",true);check(cusp.turns.size()==1);check(cusp.roots.size()==1);check(std::abs(cusp.roots[0].x-.12345)<1e-6);
 // Off-grid poles must have a branch break, and must not become roots or turns.
 for(auto expr:{L"1/(x-0.12345)",L"1/(x-0.12345)^2",L"tan(x)"}){
     auto g=math_query(expr,true);check(g.error.empty());
     if(expr!=std::wstring_view(L"tan(x)")){check(g.roots.empty());check(g.turns.empty());}
     for(size_t i=1;i<g.points.size();++i){auto a=g.points[i-1],b=g.points[i];if(a.x<.12345&&b.x>.12345&&expr!=std::wstring_view(L"tan(x)"))check(!std::isfinite(a.y)||!std::isfinite(b.y));}
 }
 auto tangentPoles=math_query(L"tan(x)",true);check(tangentPoles.turns.empty());check(tangentPoles.roots.size()==7);
 for(int k=-3;k<=2;++k){double pole=pi/2+k*pi;
     for(size_t i=1;i<tangentPoles.points.size();++i){auto a=tangentPoles.points[i-1],b=tangentPoles.points[i];if(a.x<pole&&b.x>pole)check(!std::isfinite(a.y)||!std::isfinite(b.y));}
 }
 auto domain=math_query(L"sqrt(x)",true);check(domain.error.empty());
 for(auto p:domain.points)if(p.x<0)check(!std::isfinite(p.y));
 auto bounded=math_query(L"ln(sin(x)); -10; 10; -30; 5",true);
 check(bounded.error.empty());near(bounded.ymin,-30);near(bounded.ymax,5);
 bool tail=false;for(auto p:bounded.points)if(std::isfinite(p.y)&&p.y<-12)tail=true;check(tail);
 MathViewport view{0,3,-20,2};auto zoom=math_query(L"ln(sin(x))",true,&view);
 check(zoom.error.empty());near(zoom.xmin,0);near(zoom.xmax,3);near(zoom.ymin,-20);near(zoom.ymax,2);
 MathViewport emptyView{-3,-1,-5,5};check(math_query(L"sqrt(x)",true,&emptyView).error.empty());
 for(auto expr:{L"x; 0; 1; 2; 2",L"x; 0; 1; 4; -4",L"x; 0; 1; 2",L"x; 0; 1; 2; 3; 4"})check(!math_query(expr,true).error.empty());
 // Sampling remains bounded and devotes more samples to a curved function.
 auto line=math_query(L"x",true);check(line.points.size()==601);
 check(cubeGraph.points.size()>line.points.size());check(logarithm.points.size()<50000);
 check(math_query(L"").value.empty());
 std::cout<<"Math tests passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
