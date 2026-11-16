#include "math_engine.h"
#include <cmath>
#include <cwctype>
#include <functional>
#include <iomanip>
#include <locale>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <algorithm>
namespace recall {
namespace {
class Parser {
    std::wstring_view s; size_t p=0; int depth=0; double x;
    void space(){while(p<s.size()&&iswspace(s[p]))++p;}
    bool take(wchar_t c){space();if(p<s.size()&&s[p]==c){++p;return true;}return false;}
    [[noreturn]] void fail(){throw std::runtime_error("Invalid expression or function arguments");}
    double primary(){
        if(++depth>64) fail();
        double v=0; space();
        if(take(L'(')){v=sum();if(!take(L')'))fail();}
        else if(p<s.size()&&(iswdigit(s[p])||s[p]==L'.')){
            auto start=p; bool digit=false;
            while(p<s.size()&&iswdigit(s[p])){++p;digit=true;}
            if(p<s.size()&&s[p]==L'.'){++p;while(p<s.size()&&iswdigit(s[p])){++p;digit=true;}}
            if(!digit)fail();
            if(p<s.size()&&(s[p]==L'e'||s[p]==L'E')&&p+1<s.size()&&(iswdigit(s[p+1])||s[p+1]==L'+'||s[p+1]==L'-')){
                ++p;if(p<s.size()&&(s[p]==L'+'||s[p]==L'-'))++p;
                auto exponent=p;while(p<s.size()&&iswdigit(s[p]))++p;if(p==exponent)fail();
            }
            std::wistringstream in{std::wstring(s.substr(start,p-start))};in.imbue(std::locale::classic());if(!(in>>v))fail();
        } else {
            auto start=p;while(p<s.size()&&(iswalpha(s[p])||s[p]==L'_'))++p;
            auto name=std::wstring(s.substr(start,p-start));for(auto& c:name)c=towlower(c);
            if(name==L"pi"||name==L"π")v=std::numbers::pi;
            else if(name==L"e")v=std::numbers::e;
            else if(name==L"x")v=x;
            else {
                if(!take(L'('))fail();double a=sum();bool two=take(L',');double b=two?sum():0;if(!take(L')'))fail();
                if(name==L"log")v=two?std::log(a)/std::log(b):std::log10(a);
                else {if(two)fail();
                    if(name==L"sqrt")v=std::sqrt(a);else if(name==L"ln")v=std::log(a);
                    else if(name==L"sin")v=std::sin(a);else if(name==L"cos")v=std::cos(a);else if(name==L"tan")v=std::tan(a);
                    else if(name==L"asin")v=std::asin(a);else if(name==L"acos")v=std::acos(a);else if(name==L"atan")v=std::atan(a);
                    else if(name==L"abs")v=std::abs(a);else if(name==L"exp")v=std::exp(a);else fail();
                }
                if(name==L"log"&&two&&(b<=0||b==1))fail();
            }
        }
        --depth;return v;
    }
    double power(){double a=primary();if(take(L'^'))a=std::pow(a,unary());return a;}
    double unary(){if(take(L'+')){if(++depth>64)fail();auto v=unary();--depth;return v;}if(take(L'-')){if(++depth>64)fail();auto v=-unary();--depth;return v;}return power();}
    double product(){double v=unary();for(;;){if(take(L'*'))v*=unary();else if(take(L'/'))v/=unary();else return v;}}
    double sum(){double v=product();for(;;){if(take(L'+'))v+=product();else if(take(L'-'))v-=product();else return v;}}
public:
    Parser(std::wstring_view input,double variable):s(input),x(variable){}
    double run(){if(s.size()>256)fail();double v=sum();space();if(p!=s.size())fail();if(!std::isfinite(v))throw std::runtime_error("Undefined or non-finite result");return v;}
};
std::vector<std::wstring_view> arguments(std::wstring_view s){
    std::vector<std::wstring_view> out;int depth=0;size_t start=0;
    for(size_t i=0;i<s.size();++i){if(s[i]==L'(')++depth;if(s[i]==L')')--depth;if(s[i]==L','&&depth==0){out.push_back(s.substr(start,i-start));start=i+1;}}
    out.push_back(s.substr(start));return out;
}
}
double calculate(std::wstring_view s,double x){return Parser(s,x).run();}
std::wstring math_number(double v){if(v==0)v=0;std::wostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(12)<<v;return out.str();}
MathResult math_query(std::wstring_view input,bool graph){
    MathResult r;
    try {
        while(!input.empty()&&iswspace(input.front()))input.remove_prefix(1);
        while(!input.empty()&&iswspace(input.back()))input.remove_suffix(1);
        if(input.empty()){r.info=L"Use = 1 + 1, = sqrt(9), = log(8,2), or y=sin(x).\nTrig uses radians.\nIntegral: = integral(x^2,0,1)\nGraph range: y=x^2; -5; 5";return r;}
        if(!graph){
            double value;
            if(input.starts_with(L"integral(")&&input.back()==L')'){
                auto args=arguments(input.substr(9,input.size()-10));if(args.size()!=3)throw std::runtime_error("Use integral(expression,lower,upper)");
                double a=calculate(args[1]),b=calculate(args[2]);if(a==b)value=0;else{
                    constexpr int n=4096;double h=(b-a)/n,total=calculate(args[0],a)+calculate(args[0],b);
                    double coarse=total;
                    for(int i=1;i<n;++i){double y=calculate(args[0],a+h*i);total+=(i%2?4:2)*y;if(i%2==0)coarse+=(i%4?4:2)*y;}
                    value=total*h/3;coarse*=2*h/3;
                    if(!std::isfinite(coarse)||std::abs(value-coarse)>1e-7*(1+std::abs(value)))
                        throw std::runtime_error("Integral did not converge; choose a smoother integrand or smaller interval");
                }
                r.info=L"Numerical definite integral (Simpson approximation). Requires a continuous, finite integrand; check convergence near singularities.";
            }else{ // x is reserved for graph/integral expressions.
                for(size_t i=0;i<input.size();) {
                    if(iswalpha(input[i])) { auto start=i;while(i<input.size()&&iswalpha(input[i]))++i;auto name=input.substr(start,i-start);
                        if(name==L"x"||name==L"X")throw std::runtime_error("Use y= for expressions containing x");
                    }else ++i;
                }
                value=calculate(input);
            }
            if(!std::isfinite(value))throw std::runtime_error("Undefined or non-finite result");r.value=math_number(value);if(r.info.empty())r.info=L"Enter copies this result. Trig uses radians.";return r;
        }
        auto separator=input.find(L';');auto expression=input.substr(0,separator);
        if(separator!=input.npos){auto range=input.substr(separator+1);auto second=range.find(L';');if(second==range.npos)throw std::runtime_error("Use y=expression; xmin; xmax");r.xmin=calculate(range.substr(0,second));r.xmax=calculate(range.substr(second+1));}
        if(!(r.xmax>r.xmin)||!std::isfinite(r.xmax-r.xmin))throw std::runtime_error("Graph range must be finite with xmin < xmax");
        // Validate syntax even if the selected range falls outside the domain.
        auto sample=[&](double x){try{return calculate(expression,x);}catch(const std::exception& e){if(std::string(e.what())=="Undefined or non-finite result")return static_cast<double>(NAN);throw;}};
        double low=INFINITY,high=-INFINITY;std::wstring roots,turns;int rootCount=0,turnCount=0;
        constexpr int n=600;
        for(int i=0;i<=n;++i){double x=r.xmin+(r.xmax-r.xmin)*i/n,y=sample(x);r.points.push_back({x,y});if(std::isfinite(y)){low=std::min(low,y);high=std::max(high,y);}}
        if(!std::isfinite(low))throw std::runtime_error("No finite values in this graph range");
        double span=high-low;if(!std::isfinite(span))throw std::runtime_error("Graph values exceed display range");if(span<1e-8)span=2;r.ymin=low-span*.1;r.ymax=high+span*.1;
        if(!std::isfinite(r.ymax-r.ymin)||!(r.ymax>r.ymin))throw std::runtime_error("Graph values exceed display range");
        for(int i=1;i<=n;++i){auto a=r.points[i-1],b=r.points[i];if(!std::isfinite(a.y)||!std::isfinite(b.y))continue;
            if(rootCount<6&&(a.y==0||(a.y<0)!=(b.y<0))){double l=a.x,h=b.x,fl=a.y;for(int j=0;j<40&&a.y!=0;++j){double mid=(l+h)/2,f=sample(mid);if(!std::isfinite(f))break;if((fl<0)==(f<0)){l=mid;fl=f;}else h=mid;}double root=a.y==0?a.x:(l+h)/2;if(std::abs(sample(root))<1e-6){roots+=math_number(root)+L"  ";++rootCount;}}
            if(i<n&&turnCount<6){auto c=r.points[i+1];if(std::isfinite(c.y)&&((b.y>a.y&&b.y>c.y)||(b.y<a.y&&b.y<c.y))){turns+=L"("+math_number(b.x)+L", "+math_number(b.y)+L")  ";++turnCount;}}
        }
        r.info=L"x: ["+math_number(r.xmin)+L", "+math_number(r.xmax)+L"]\ny: ["+math_number(r.ymin)+L", "+math_number(r.ymax)+L"]\nApprox. x-intercepts: "+(roots.empty()?L"none detected":roots)+L"\nApprox. turning points: "+(turns.empty()?L"none detected":turns);
        double intercept=sample(0);if(std::isfinite(intercept))r.info+=L"\ny-intercept: "+math_number(intercept);
        r.info+=L"\nSampled estimates; may miss roots, extrema, or discontinuities.\nArea: = integral(expression,a,b)";
    }catch(const std::exception& e){r.points.clear();r.value.clear();r.error=std::wstring(e.what(),e.what()+std::char_traits<char>::length(e.what()));}
    return r;
}
}
