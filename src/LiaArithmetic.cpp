#include "LiaArithmetic.hpp"
#include <algorithm>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>

namespace lia {
namespace {
using Rows = std::vector<Inequality>;
constexpr int MAX_PROJECTIONS = 2500;
constexpr int MAX_NODES = 1500;
constexpr int MAX_DEPTH = 96;

Int absolute(Int x) { return x < 0 ? checkedNegate(x) : x; }
Int gcd(Int a, Int b) {
    a = absolute(a); b = absolute(b);
    while (b) { const Int r = a % b; a = b; b = r; }
    return a;
}
struct Rational {
    Int n=0, d=1;
    Rational()=default;
    explicit Rational(Int x):n(x){}
    Rational(Int numerator, Int denominator):n(numerator),d(denominator) {
        if (!d) throw std::logic_error("division by zero");
        if (d < 0) { n=checkedNegate(n); d=checkedNegate(d); }
        Int div=gcd(n,d);
        if (div) { n/=div; d/=div; }
    }
};
Rational plus(const Rational&a,const Rational&b) {
    const Int g=gcd(a.d,b.d);
    const Int lhs=checkedMultiply(a.n,b.d/g);
    const Int rhs=checkedMultiply(b.n,a.d/g);
    return {checkedAdd(lhs,rhs),checkedMultiply(a.d,b.d/g)};
}
Rational neg(const Rational&a) {return {checkedNegate(a.n),a.d};}
Rational minus(const Rational&a,const Rational&b) {return plus(a,neg(b));}
Rational times(const Rational&a, Int b) {
    const Int g=gcd(a.d,b);
    return {checkedMultiply(a.n,b/g),a.d/g};
}
Rational divide(const Rational&a,Int b) {
    if (!b) throw std::logic_error("division by zero");
    const Int g=gcd(a.n,b);
    return {a.n/g,checkedMultiply(a.d,b/g)};
}
bool less(const Rational&a,const Rational&b) {
    // denominator factors reduced to avoid unnecessary overflow.
    const Int g=gcd(a.d,b.d);
    return checkedMultiply(a.n,b.d/g)<checkedMultiply(b.n,a.d/g);
}
Int floor(const Rational&r) {return floorDivide(r.n,r.d);}
void normalize(Inequality& row) {
    Int common=0;
    for (Int c:row.coefficients) common=gcd(common,c);
    if (common>1) {
        for (Int& c:row.coefficients)c/=common;
        // Integer-domain rounding: the LHS is divisible by common.
        row.rhs=floorDivide(row.rhs,common);
    }
}
bool contradictory(const Inequality&r) {
    return std::all_of(r.coefficients.begin(),r.coefficients.end(),
        [](Int c){return c==0;}) && r.rhs<0;
}
struct Projection {
    bool contradiction=false,unknown=false;
    std::vector<Rows> layers;
    std::vector<Rational> values;
    std::string reason;
};
Projection project(const Rows& constraints,int n) {
    Projection result;
    result.layers.resize(static_cast<size_t>(n));
    Rows current=constraints;
    for (Inequality&r:current)normalize(r);
    for(int variable=n-1;variable>=0;--variable) {
        if (current.size()>MAX_PROJECTIONS) {
            result.unknown=true;result.reason="Fourier-Motzkin projection budget";return result;
        }
        for(const Inequality&r:current)
            if(contradictory(r)){result.contradiction=true;return result;}
        result.layers[static_cast<size_t>(variable)]=current;
        Rows next;
        std::vector<const Inequality*>positive,negative;
        for(const Inequality&r:current) {
            Int v=r.coefficients[static_cast<size_t>(variable)];
            if(v>0)positive.push_back(&r);
            else if(v<0)negative.push_back(&r);
            else next.push_back(r);
        }
        if(positive.size()*negative.size()>MAX_PROJECTIONS) {
            result.unknown=true;result.reason="Projection pair budget";return result;
        }
        for(const Inequality*p:positive)for(const Inequality*m:negative) {
            const Int a=p->coefficients[static_cast<size_t>(variable)];
            const Int b=checkedNegate(m->coefficients[static_cast<size_t>(variable)]);
            const Int g=gcd(a,b);
            const Int pa=b/g, mb=a/g;
            Inequality row;
            row.coefficients.resize(static_cast<size_t>(n));
            for(int j=0;j<n;++j) {
                row.coefficients[static_cast<size_t>(j)]=checkedAdd(
                    checkedMultiply(pa,p->coefficients[static_cast<size_t>(j)]),
                    checkedMultiply(mb,m->coefficients[static_cast<size_t>(j)]));
            }
            row.rhs=checkedAdd(checkedMultiply(pa,p->rhs),checkedMultiply(mb,m->rhs));
            normalize(row);
            if(contradictory(row)){result.contradiction=true;return result;}
            next.push_back(std::move(row));
        }
        current=std::move(next);
    }
    for(const Inequality&r:current)if(contradictory(r)){
        result.contradiction=true;return result;
    }
    // Reconstruct an exact rational feasible point (not an integer model yet).
    result.values.resize(static_cast<size_t>(n));
    for(int variable=0;variable<n;++variable) {
        std::optional<Rational>lower,upper;
        for(const Inequality&r:result.layers[static_cast<size_t>(variable)]) {
            Rational rhs(r.rhs);
            for(int j=0;j<variable;++j)
                rhs=minus(rhs,times(result.values[static_cast<size_t>(j)],
                                     r.coefficients[static_cast<size_t>(j)]));
            const Int coeff=r.coefficients[static_cast<size_t>(variable)];
            if(coeff>0) {
                Rational bound=divide(rhs,coeff);
                if(!upper || less(bound,*upper))upper=bound;
            } else if(coeff<0) {
                Rational bound=divide(rhs,coeff);
                if(!lower || less(*lower,bound))lower=bound;
            } else if(less(rhs,Rational(0))) {
                result.contradiction=true;return result;
            }
        }
        if(lower&&upper&&less(*upper,*lower)){
            result.contradiction=true;return result;
        }
        Rational zero(0);
        if(lower&&less(zero,*lower))result.values[static_cast<size_t>(variable)]=*lower;
        else if(upper&&less(*upper,zero))result.values[static_cast<size_t>(variable)]=*upper;
        else result.values[static_cast<size_t>(variable)]=zero;
    }
    return result;
}
struct Budget {int visits=0;int branches=0;};
ArithmeticResult recurse(const Rows& constraints,int n,int depth,Budget&budget) {
    if(depth>MAX_DEPTH || ++budget.visits>MAX_NODES)
        return {ArithmeticResult::State::Unknown,{},"Integer branch budget",budget.branches};
    Projection p=project(constraints,n);
    if(p.unknown)return {ArithmeticResult::State::Unknown,{},p.reason,budget.branches};
    if(p.contradiction)return {ArithmeticResult::State::Unsat,{},"Rational infeasibility / gcd cut",budget.branches};
    int fractional=-1;
    for(int i=0;i<n;++i)if(p.values[static_cast<size_t>(i)].d != 1){
        fractional=i;break;
    }
    if(fractional<0){
        std::vector<Int>model;
        for(const Rational&r:p.values)model.push_back(r.n);
        // Explicit exact model verification: SAT must never come from a
        // floating-point or incomplete guess.
        for(const Inequality&r:constraints){
            Int lhs=0;
            for(int i=0;i<n;++i)lhs=checkedAdd(lhs,
               checkedMultiply(r.coefficients[static_cast<size_t>(i)],model[static_cast<size_t>(i)]));
            if(lhs>r.rhs)throw std::logic_error("invalid integer model");
        }
        return {ArithmeticResult::State::Sat,model,{},budget.branches};
    }
    const Int split=floor(p.values[static_cast<size_t>(fractional)]);
    Inequality left,right;
    left.coefficients.resize(static_cast<size_t>(n));
    right.coefficients.resize(static_cast<size_t>(n));
    left.coefficients[static_cast<size_t>(fractional)]=1;
    right.coefficients[static_cast<size_t>(fractional)]=-1;
    left.rhs=split;
    right.rhs=checkedNegate(checkedAdd(split,1));
    ++budget.branches;
    Rows branch=constraints;
    branch.push_back(left);
    ArithmeticResult a=recurse(branch,n,depth+1,budget);
    if(a.state==ArithmeticResult::State::Sat)return a;
    branch.back()=right;
    ArithmeticResult b=recurse(branch,n,depth+1,budget);
    if(b.state==ArithmeticResult::State::Sat)return b;
    if(a.state==ArithmeticResult::State::Unknown || b.state==ArithmeticResult::State::Unknown)
        return {ArithmeticResult::State::Unknown,{},"Arithmetic branch incomplete",budget.branches};
    return {ArithmeticResult::State::Unsat,{},"Both integer branches infeasible",budget.branches};
}
}
Int checkedAdd(Int a,Int b) {
    Int result;
    if(__builtin_add_overflow(a,b,&result))throw std::overflow_error("128-bit arithmetic overflow");
    return result;
}
Int checkedMultiply(Int a,Int b) {
    Int result;
    if(__builtin_mul_overflow(a,b,&result))throw std::overflow_error("128-bit arithmetic overflow");
    return result;
}
Int checkedNegate(Int a) {return checkedMultiply(a,Int(-1));}
Int floorDivide(Int a,Int positive) {
    if(positive<=0)throw std::logic_error("positive denominator expected");
    Int quotient=a/positive;
    if(a<0 && a%positive)quotient=checkedAdd(quotient,-1);
    return quotient;
}
std::string printInt(Int value) {
    if(value==0)return "0";
    bool negative=value<0;
    if(negative)value=checkedNegate(value);
    std::string result;
    while(value){result.push_back(char('0'+int(value%10)));value/=10;}
    if(negative)result.push_back('-');
    std::reverse(result.begin(),result.end());
    return result;
}
Int parseInt(const std::string&token) {
    if(token.empty())throw std::invalid_argument("empty integer token");
    size_t i=0;bool negative=false;
    if(token[i]=='-'){negative=true;++i;}
    if(i==token.size())throw std::invalid_argument("invalid numeral");
    Int value=0;
    for(;i<token.size();++i) {
        if(token[i]<'0'||token[i]>'9')throw std::invalid_argument("invalid integer numeral: "+token);
        value=checkedAdd(checkedMultiply(value,10),Int(token[i]-'0'));
    }
    return negative?checkedNegate(value):value;
}
ArithmeticResult checkIntegers(const std::vector<Inequality>& constraints,int nvars) {
    if(nvars<0 || nvars>32)
        return {ArithmeticResult::State::Unknown,{},"Variable limit (32)",0};
    try {
        for(const Inequality&r:constraints)
            if(r.coefficients.size()!=static_cast<size_t>(nvars))
                throw std::logic_error("constraint dimension mismatch");
        Budget budget;
        return recurse(constraints,nvars,0,budget);
    } catch(const std::overflow_error&error) {
        return {ArithmeticResult::State::Unknown,{},error.what(),0};
    }
}
}
