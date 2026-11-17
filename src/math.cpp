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
#include <limits>
#include <numeric>
namespace recall {
namespace {
double real_power(double base,double exponent){
    if(base>=0||!std::isfinite(exponent)||exponent==std::trunc(exponent))return std::pow(base,exponent);
    // Recover small rational exponents using continued fractions. Odd roots
    // of negative numbers are real; even roots and irrational powers are not.
    double target=std::abs(exponent),remainder=target;
    double p0=0,p1=1,q0=1,q1=0;
    for(int i=0;i<32;++i){
        double a=std::floor(remainder),p=a*p1+p0,q=a*q1+q0;
        if(!std::isfinite(p)||q>1000000||p>9007199254740991.0)break;
        if(q>0&&std::abs(p/q-target)<=std::numeric_limits<double>::epsilon()*target){
            if(std::fmod(q,2)==0)break;
            double magnitude=std::pow(-base,exponent);
            return std::fmod(p,2)==0?magnitude:-magnitude;
        }
        double fraction=remainder-a;if(fraction==0)break;
        p0=p1;p1=p;q0=q1;q1=q;remainder=1/fraction;
    }
    return std::pow(base,exponent);
}
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
    double power(){double a=primary();if(take(L'^'))a=real_power(a,unary());return a;}
    double unary(){if(take(L'+')){if(++depth>64)fail();auto v=unary();--depth;return v;}if(take(L'-')){if(++depth>64)fail();auto v=-unary();--depth;return v;}return power();}
    double product(){
        double v=unary();
        for(;;){
            if(take(L'*'))v*=unary();
            else if(take(L'/'))v/=unary();
            else {
                // Juxtaposed factors, e.g. 2x, 2pi, 2sin(x), or (x+1)(x-1).
                // Keep the same left-to-right precedence as explicit * and /.
                space();
                if(p<s.size()&&(s[p]==L'('||iswalpha(s[p])))v*=unary();
                else return v;
            }
        }
    }
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
MathResult math_query(std::wstring_view input,bool graph,const MathViewport* viewport){
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
        std::vector<std::wstring_view> bounds;
        size_t start=0;
        for(size_t i=0;i<=input.size();++i)if(i==input.size()||input[i]==L';'){bounds.push_back(input.substr(start,i-start));start=i+1;}
        auto expression=bounds[0];
        if(bounds.size()!=1&&bounds.size()!=3&&bounds.size()!=5)throw std::runtime_error("Use y=expression; xmin; xmax[; ymin; ymax]");
        if(bounds.size()>=3){r.xmin=calculate(bounds[1]);r.xmax=calculate(bounds[2]);}
        bool fixedY=bounds.size()==5||viewport;
        if(bounds.size()==5){r.ymin=calculate(bounds[3]);r.ymax=calculate(bounds[4]);}
        if(viewport){r.xmin=viewport->xmin;r.xmax=viewport->xmax;r.ymin=viewport->ymin;r.ymax=viewport->ymax;}
        auto valid=[](double lo,double hi){return std::isfinite(lo)&&std::isfinite(hi)&&hi>lo&&std::isfinite(hi-lo);};
        if(!valid(r.xmin,r.xmax)||(fixedY&&!valid(r.ymin,r.ymax)))throw std::runtime_error("Graph bounds must be finite with min < max");
        auto sample=[&](double x){try{return calculate(expression,x);}catch(const std::exception& e){if(std::string(e.what())=="Undefined or non-finite result")return static_cast<double>(NAN);throw;}};
        constexpr int n=600;
        std::vector<MathPoint> seed;std::vector<double> values;
        for(int i=0;i<=n;++i){double x=std::lerp(r.xmin,r.xmax,static_cast<double>(i)/n),y=sample(x);seed.push_back({x,y});if(std::isfinite(y))values.push_back(y);}
        if(values.empty()&&!fixedY)throw std::runtime_error("No finite values in this graph range");
        if(!fixedY){
            // Trim isolated spikes so poles do not flatten the initial view.
            std::sort(values.begin(),values.end());size_t trim=values.size()>100?values.size()/100:0;
            double low=values[trim],high=values[values.size()-1-trim],span=high-low;
            if(!std::isfinite(span))throw std::runtime_error("Graph values exceed display range");
            if(span<1e-8)span=2;r.ymin=low-span*.1;r.ymax=high+span*.1;
        }
        if(!valid(r.ymin,r.ymax))throw std::runtime_error("Graph values exceed display range");
        const double yspan=r.ymax-r.ymin;
        // Refine screen-space midpoint error. NaN vertices separate branches.
        size_t evaluations=0;
        auto screenY=[&](double y){return std::clamp((y-r.ymin)/yspan,-2.0,3.0)*320;};
        std::function<void(MathPoint,MathPoint,int)> refine;
        refine=[&](MathPoint a,MathPoint b,int depth){
            double mx=std::midpoint(a.x,b.x);
            if(mx==a.x||mx==b.x){r.points.push_back(b);return;}
            MathPoint m{mx,sample(mx)};++evaluations;
            bool fa=std::isfinite(a.y),fb=std::isfinite(b.y),fm=std::isfinite(m.y);
            bool mixed=(fa!=fb)||(fa!=fm);
            double error=fa&&fb&&fm?std::abs(screenY(m.y)-(screenY(a.y)+screenY(b.y))*.5):0;
            double scale=std::max({1.0,std::abs(a.y),std::abs(b.y),std::abs(m.y)});
            double relative=fa&&fb&&fm?std::abs(m.y/scale-(a.y/scale+b.y/scale)*.5):0;
            // Keep probing offscreen curvature too: both sides of an even
            // order pole can lie above the viewport and have the same sign.
            bool split=mixed||error>.4||relative>.1;
            if(split&&depth<18&&evaluations<24000){refine(a,m,depth+1);refine(m,b,depth+1);}
            else{if(split&&(mixed||error>2||relative>.1))r.points.push_back({mx,NAN});r.points.push_back(b);}
        };
        r.points.push_back(seed.front());for(int i=1;i<=n;++i)refine(seed[i-1],seed[i],0);
        auto add=[](std::vector<MathPoint>& list,MathPoint point,double tolerance){for(auto other:list)if(std::abs(other.x-point.x)<=tolerance)return;list.push_back(point);};
        const double xtol=(r.xmax-r.xmin)*1e-8;
        // Refined extrema also reveal roots that touch the axis without crossing.
        for(int i=1;i<n;++i){
            auto a=seed[i-1],b=seed[i],c=seed[i+1];
            if(!std::isfinite(a.y)||!std::isfinite(b.y)||!std::isfinite(c.y))continue;
            bool minimum=b.y<=a.y&&b.y<=c.y&&(b.y<a.y||b.y<c.y);
            bool maximum=b.y>=a.y&&b.y>=c.y&&(b.y>a.y||b.y>c.y);
            if(!minimum&&!maximum)continue;
            double l=a.x,h=c.x;
            for(int j=0;j<72;++j){
                double u=std::lerp(l,h,.3819660112501051),v=std::lerp(l,h,.6180339887498949);
                double fu=sample(u),fv=sample(v);if(!std::isfinite(fu)||!std::isfinite(fv))break;
                if(minimum?fu<fv:fu>fv)h=v;else l=u;
            }
            double x=std::midpoint(l,h),y=sample(x);if(!std::isfinite(y))continue;
            if(b.y==y)x=b.x; // Prefer an exact sampled peak to a rounded plateau edge.
            // A pole fails this local convergence check; a finite cusp may
            // still be a valid extremum even when its derivative is undefined.
            double delta=(c.x-a.x)*1e-4,yl=sample(x-delta),yr=sample(x+delta);
            double tolerance=1e-3*(1+std::abs(y)+std::max(std::abs(a.y-y),std::abs(c.y-y)));
            if(!std::isfinite(yl)||!std::isfinite(yr)||std::abs(yl-y)>tolerance||std::abs(yr-y)>tolerance)continue;
            add(r.turns,{x,y},xtol);
            if(std::abs(y)<1e-10)add(r.roots,{x,0},xtol);
        }
        for(size_t i=0;i<r.points.size();++i){
            auto b=r.points[i];if(b.y==0&&(i==0||r.points[i-1].y!=0))add(r.roots,b,xtol);
            if(i==0)continue;auto a=r.points[i-1];
            if(!std::isfinite(a.y)||!std::isfinite(b.y)||a.y==0||b.y==0||(a.y<0)==(b.y<0))continue;
            double l=a.x,h=b.x,fl=a.y;bool finite=true;
            for(int j=0;j<64;++j){double mid=std::midpoint(l,h),f=sample(mid);if(!std::isfinite(f)){finite=false;break;}if(f==0){l=h=mid;break;}if((fl<0)==(f<0)){l=mid;fl=f;}else h=mid;}
            double root=std::midpoint(l,h),y=sample(root);
            if(finite&&std::isfinite(y)&&std::abs(y)<1e-8)add(r.roots,{root,0},xtol);
        }
        auto order=[](MathPoint a,MathPoint b){return a.x<b.x;};
        std::sort(r.roots.begin(),r.roots.end(),order);std::sort(r.turns.begin(),r.turns.end(),order);
        std::wstring roots,turns;
        for(size_t i=0;i<std::min(size_t(6),r.roots.size());++i)roots+=math_number(r.roots[i].x)+L"  ";
        for(size_t i=0;i<std::min(size_t(6),r.turns.size());++i)turns+=L"("+math_number(r.turns[i].x)+L", "+math_number(r.turns[i].y)+L")  ";
        r.info=L"x: ["+math_number(r.xmin)+L", "+math_number(r.xmax)+L"]\ny: ["+math_number(r.ymin)+L", "+math_number(r.ymax)+L"]\nApprox. x-intercepts: "+(roots.empty()?L"none detected":roots)+L"\nApprox. turning points: "+(turns.empty()?L"none detected":turns);
        double intercept=sample(0);if(std::isfinite(intercept))r.info+=L"\ny-intercept: "+math_number(intercept);
        r.info+=L"\nNumerical estimates; narrow features or discontinuities may be missed.\nArea: = integral(expression,a,b)";
    }catch(const std::exception& e){r.points.clear();r.value.clear();r.error=std::wstring(e.what(),e.what()+std::char_traits<char>::length(e.what()));}
    return r;
}
}
