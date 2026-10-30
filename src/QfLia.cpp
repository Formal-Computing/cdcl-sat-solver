#include "QfLia.hpp"
#include "LiaArithmetic.hpp"
#include "Solver.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace lia {
namespace {
struct Unsupported : std::runtime_error {using std::runtime_error::runtime_error;};
struct Node {
    std::string text;
    std::vector<Node> children;
    bool isList=false;
};
std::vector<std::string> tokens(const std::string& input) {
    std::vector<std::string> out;
    for(size_t i=0;i<input.size();) {
        const unsigned char ch=static_cast<unsigned char>(input[i]);
        if(std::isspace(ch)){++i;continue;}
        if(input[i]==';'){while(i<input.size()&&input[i]!='\n')++i;continue;}
        if(input[i]=='(' || input[i]==')'){out.push_back(input.substr(i,1));++i;continue;}
        if(input[i]=='|') {
            const size_t start=++i;
            while(i<input.size()&&input[i]!='|')++i;
            if(i==input.size())throw Unsupported("Unclosed quoted symbol");
            out.push_back(input.substr(start,i-start));++i;continue;
        }
        // String tokens are needed in metadata (set-info), even though
        // arithmetic/Boolean expressions cannot contain strings.
        if(input[i]=='"') {
            const size_t start=i++;
            bool closed=false;
            while(i<input.size()) {
                if(input[i]=='"') {
                    if(i+1<input.size() && input[i+1]=='"'){i+=2;continue;}
                    ++i;closed=true;break;
                }
                ++i;
            }
            if(!closed)throw Unsupported("Unterminated SMT-LIB string");
            out.push_back(input.substr(start,i-start));
            continue;
        }
        const size_t start=i;
        while(i<input.size()&&!std::isspace(static_cast<unsigned char>(input[i]))
              &&input[i]!='('&&input[i]!=')'&&input[i]!=';')++i;
        if(start==i)throw Unsupported("Bad SMT token");
        out.push_back(input.substr(start,i-start));
    }
    return out;
}
Node parseNode(const std::vector<std::string>& lex,size_t&cursor) {
    if(cursor>=lex.size())throw Unsupported("Unexpected end of SMT-LIB input");
    if(lex[cursor]==")")throw Unsupported("Unexpected )");
    if(lex[cursor]!="(")return {lex[cursor++],{},false};
    ++cursor;Node node;node.isList=true;
    while(cursor<lex.size() && lex[cursor]!=")")node.children.push_back(parseNode(lex,cursor));
    if(cursor>=lex.size())throw Unsupported("Missing )");
    ++cursor;return node;
}
std::string text(const Node&node) {
    if(!node.isList)return node.text;
    std::string out="(";
    for(size_t i=0;i<node.children.size();++i) {
        if(i)out+=" ";
        out+=text(node.children[i]);
    }
    return out+")";
}
std::string head(const Node&node) {
    if(!node.isList||node.children.empty()||node.children[0].isList)
        throw Unsupported("Malformed SMT-LIB expression: "+text(node));
    return node.children[0].text;
}
using Terms = std::vector<Node>;
struct Linear {
    std::vector<Int> a;
    Int c=0; // a*x + c
};
class Builder {
public:
    int nvars=0;
    std::vector<std::string> symbols;
    std::map<std::string,int> declared;
    std::map<std::string,int> declaredBoolean;
    sat::CnfFormula cnf;
    std::vector<Inequality> atoms;
    std::vector<std::string> descriptions;
    std::vector<int> satVariables;
    std::map<std::string,int> interned;
    struct Binding {
        bool boolType=false;
        Linear linear;
        int booleanLiteral=0;
    };
    using Scope=std::map<std::string,Binding>;
    std::vector<Scope> scopes;
    explicit Builder(const Terms&commands) {
        // Discover variable declarations first.
        for(const Node&command:commands) {
            const std::string op=head(command);
            if(op=="declare-const") {
                if(command.children.size()!=3 || command.children[1].isList)
                    throw Unsupported("Malformed declare-const: "+text(command));
                const std::string sort=text(command.children[2]);
                if(sort=="Int")addVar(command.children[1].text);
                else if(sort=="Bool")addBoolean(command.children[1].text);
                else throw Unsupported("Only Int and Bool declarations supported");
            } else if(op=="declare-fun") {
                if(command.children.size()!=4 || command.children[1].isList
                   || !command.children[2].isList||!command.children[2].children.empty())
                    throw Unsupported("Only nullary functions supported");
                const std::string sort=text(command.children[3]);
                if(sort=="Int")addVar(command.children[1].text);
                else if(sort=="Bool")addBoolean(command.children[1].text);
                else throw Unsupported("Only Int and Bool declarations supported");
            } else if(op=="set-logic" && (command.children.size()!=2 ||
                                          text(command.children[1])!="QF_LIA"))
                throw Unsupported("Only (set-logic QF_LIA) is supported");
        }
        if(nvars>32)throw Unsupported("Native QF_LIA currently supports up to 32 integer variables");
        bool checked=false,exited=false;
        for(const Node&command:commands) {
            if(exited)throw Unsupported("Commands after exit are not supported");
            const std::string op=head(command);
            if(op=="assert"){
                if(checked)throw Unsupported("assert after check-sat would change the query");
                if(command.children.size()!=2)throw Unsupported("assert expects one Boolean argument");
                cnf.clauses.push_back({boolean(command.children[1])});
            }else if(op=="set-logic"||op=="set-info"||op=="set-option"||
                     op=="declare-const"||op=="declare-fun") {
                if(checked)throw Unsupported("Declarations or settings after check-sat unsupported");
            }else if(op=="get-model"||op=="get-info") {
                if(!checked)throw Unsupported("Query command before check-sat");
            }else if(op=="exit") {
                if(command.children.size()!=1)throw Unsupported("exit takes no arguments");
                exited=true;
            }else if(op=="check-sat") {
                if(command.children.size()!=1)throw Unsupported("check-sat takes no arguments");
                ++checks;checked=true;
            }else throw Unsupported("Unsupported SMT command: "+op);
        }
        if(checks!=1)throw Unsupported("Exactly one check-sat is required");
        cnf.variableCount=nextSat-1;
    }
    int checks=0;
private:
    int nextSat=1;
    void addVar(const std::string&name) {
        if(declared.count(name)||declaredBoolean.count(name))
            throw Unsupported("Duplicate variable: "+name);
        declared[name]=nvars++;
        symbols.push_back(name);
    }
    void addBoolean(const std::string&name) {
        if(declared.count(name)||declaredBoolean.count(name))
            throw Unsupported("Duplicate variable: "+name);
        declaredBoolean[name]=var();
    }
    int var(){return nextSat++;}
    const Binding* bound(const std::string&name) const {
        for(auto it=scopes.rbegin();it!=scopes.rend();++it) {
            const auto found=it->find(name);
            if(found!=it->end())return &found->second;
        }
        return nullptr;
    }
    std::vector<std::pair<std::string,Node>> letEntries(const Node&node) const {
        if(node.children.size()!=3 || !node.children[1].isList)
            throw Unsupported("let expects bindings and one body");
        std::vector<std::pair<std::string,Node>> entries;
        std::set<std::string> seen;
        for(const Node&b:node.children[1].children) {
            if(!b.isList||b.children.size()!=2||b.children[0].isList)
                throw Unsupported("Malformed let binding");
            const auto&name=b.children[0].text;
            if(!seen.insert(name).second)throw Unsupported("Duplicate let binding: "+name);
            entries.emplace_back(name,b.children[1]);
        }
        return entries;
    }
    // SMT-LIB let has simultaneous bindings: evaluate all RHS in OUTER scope.
    Scope evaluatedLet(const Node&node) {
        Scope result;
        const auto entries=letEntries(node);
        for(const auto&[name,value]:entries) {
            Binding binding;
            binding.boolType=isBoolean(value);
            if(binding.boolType)binding.booleanLiteral=boolean(value);
            else binding.linear=term(value);
            result.emplace(name,std::move(binding));
        }
        return result;
    }
    bool isBoolean(const Node&node) {
        if(!node.isList) {
            if(const Binding* b=bound(node.text))return b->boolType;
            return node.text=="true"||node.text=="false"||
                   declaredBoolean.count(node.text)>0;
        }
        if(node.children.empty()||node.children[0].isList)return false;
        const auto&op=node.children[0].text;
        if(op=="!") {
            if(node.children.size()<2)throw Unsupported("Malformed annotation");
            return isBoolean(node.children[1]);
        }
        if(op=="let") {
            Scope types;
            const auto entries=letEntries(node);
            for(const auto&[name,value]:entries) {
                Binding binding;
                binding.boolType=isBoolean(value);
                types.emplace(name,std::move(binding));
            }
            scopes.push_back(std::move(types));
            const bool result=isBoolean(node.children[2]);
            scopes.pop_back();
            return result;
        }
        if(op=="ite") {
            if(node.children.size()!=4)throw Unsupported("ite expects three arguments");
            const bool a=isBoolean(node.children[2]),b=isBoolean(node.children[3]);
            if(a!=b)throw Unsupported("ite branch sort mismatch");
            return a;
        }
        return op=="and"||op=="or"||op=="not"||op=="=>"||
               op=="xor"||op=="="||op=="distinct"||
               op=="<"||op=="<="||op==">"||op==">=";
    }
    Linear blank() const {return {std::vector<Int>(static_cast<size_t>(nvars),0),0};}
    void widen(Linear&line) const {line.a.resize(static_cast<size_t>(nvars),0);}
    Linear numeral(Int v) const {Linear out=blank();out.c=v;return out;}
    Linear freshInteger() {
        if(nvars>=32)throw Unsupported("QF_LIA supports up to 32 integer variables including auxiliaries");
        const int index=nvars++;
        for(Inequality&atom:atoms)atom.coefficients.push_back(0);
        interned.clear(); // previously interned keys used fewer integer columns
        Linear out=blank();
        out.a[static_cast<size_t>(index)]=1;
        return out;
    }
    void accumulate(Linear&dest,Linear source,Int scale=1) {
        widen(dest);widen(source);
        for(int i=0;i<nvars;++i)dest.a[static_cast<size_t>(i)]=checkedAdd(
            dest.a[static_cast<size_t>(i)],checkedMultiply(scale,source.a[static_cast<size_t>(i)]));
        dest.c=checkedAdd(dest.c,checkedMultiply(scale,source.c));
    }
    Inequality linearLessEqual(Linear x,Linear y,Int margin=0) {
        accumulate(x,y,-1);
        Inequality out;
        out.coefficients=std::move(x.a);
        out.rhs=checkedAdd(checkedNegate(x.c),margin);
        return out;
    }
    int linearEquality(const Linear&x,const Linear&y,const std::string&reason) {
        return combine(true,{
            atom(linearLessEqual(x,y),reason+" [<=]"),
            atom(linearLessEqual(y,x),reason+" [>=]")
        });
    }
    void requireTheory(int literal) {cnf.clauses.push_back({literal});}
    Linear term(const Node&node) {
        if(!node.isList) {
            if(const Binding*binding=bound(node.text)) {
                if(binding->boolType)throw Unsupported("Expected Int, found Bool: "+node.text);
                Linear out=binding->linear;
                widen(out);
                return out;
            }
            Linear out=blank();
            const auto it=declared.find(node.text);
            if(it!=declared.end()){out.a[static_cast<size_t>(it->second)]=1;return out;}
            try {out.c=parseInt(node.text);return out;}
            catch(const std::invalid_argument&){throw Unsupported("Undeclared Int symbol "+node.text);}
        }
        const std::string op=head(node);const size_t count=node.children.size();
        if(op=="let") {
            scopes.push_back(evaluatedLet(node));
            Linear result=term(node.children[2]);
            scopes.pop_back();
            widen(result);
            return result;
        }
        if(op=="!") {
            if(count<2)throw Unsupported("Annotation missing Int term");
            return term(node.children[1]);
        }
        if(op=="ite") {
            if(count!=4 || !isBoolean(node.children[1]))
                throw Unsupported("ite requires a Boolean condition");
            if(isBoolean(node.children[2])||isBoolean(node.children[3]))
                throw Unsupported("Expected Int branches in arithmetic ite");
            const int condition=boolean(node.children[1]);
            Linear ifTrue=term(node.children[2]);
            Linear ifFalse=term(node.children[3]);
            Linear result=freshInteger();
            const int trueEq=linearEquality(result,ifTrue,"ite-then");
            const int falseEq=linearEquality(result,ifFalse,"ite-else");
            cnf.clauses.push_back({-condition,trueEq});
            cnf.clauses.push_back({condition,falseEq});
            return result;
        }
        if(op=="div"||op=="mod") {
            if(count!=3)throw Unsupported(op+" expects two Int terms");
            Linear dividend=term(node.children[1]);
            Linear divisor=term(node.children[2]);
            widen(divisor);
            const bool constant=std::all_of(divisor.a.begin(),divisor.a.end(),
                                            [](Int value){return value==0;});
            if(!constant)throw Unsupported("Variable divisors are outside native linear arithmetic");
            const Int d=divisor.c;
            if(d==0)throw Unsupported("Division by zero has underspecified SMT-LIB semantics");
            const Int absD=d<0?checkedNegate(d):d;
            Linear quotient=freshInteger();
            Linear remainder=freshInteger();
            Linear expanded=blank();
            accumulate(expanded,quotient,d);
            accumulate(expanded,remainder);
            requireTheory(linearEquality(dividend,expanded,"Euclidean division identity"));
            requireTheory(atom(linearLessEqual(numeral(0),remainder),"remainder >= 0"));
            requireTheory(atom(linearLessEqual(remainder,numeral(absD-1)),
                               "remainder < abs(divisor)"));
            return op=="div"?quotient:remainder;
        }
        if(op=="+") {
            Linear out=blank();
            for(size_t i=1;i<count;++i)accumulate(out,term(node.children[i]));
            return out;
        }
        if(op=="-") {
            if(count<2)throw Unsupported("- needs arguments");
            Linear out=blank();
            if(count==2){accumulate(out,term(node.children[1]),-1);return out;}
            out=term(node.children[1]);
            for(size_t i=2;i<count;++i)accumulate(out,term(node.children[i]),-1);
            return out;
        }
        if(op=="*") {
            if(count<2)throw Unsupported("multiplication requires arguments");
            Linear out=blank();out.c=1;
            for(size_t i=1;i<count;++i) {
                Linear other=term(node.children[i]);
                widen(out);widen(other);
                bool leftConstant=std::all_of(out.a.begin(),out.a.end(),[](Int v){return v==0;});
                bool rightConstant=std::all_of(other.a.begin(),other.a.end(),[](Int v){return v==0;});
                if(!leftConstant && !rightConstant)
                    throw Unsupported("Nonlinear multiplication is not QF_LIA");
                if(rightConstant) {
                    for(Int& v:out.a)v=checkedMultiply(v,other.c);
                    out.c=checkedMultiply(out.c,other.c);
                } else {
                    for(Int&v:other.a)v=checkedMultiply(v,out.c);
                    other.c=checkedMultiply(other.c,out.c);
                    out=std::move(other);
                }
            }
            return out;
        }
        throw Unsupported("Unsupported integer term "+op);
    }
    int atom(Inequality inequality,const std::string&description) {
        std::string key;
        for(Int c:inequality.coefficients)key+=printInt(c)+",";
        key+="<="+printInt(inequality.rhs);
        const auto it=interned.find(key);
        if(it!=interned.end())return it->second;
        const int satVar=var();
        interned[key]=satVar;atoms.push_back(std::move(inequality));
        descriptions.push_back(description);satVariables.push_back(satVar);
        return satVar;
    }
    int constant(bool value) {
        const int x=var();
        cnf.clauses.push_back({value?x:-x});
        return x;
    }
    int combine(bool conjunction,const std::vector<int>&literals) {
        if(literals.empty())return constant(conjunction);
        if(literals.size()==1)return literals[0];
        const int v=var();
        sat::Clause clause{conjunction?v:-v};
        for(const int lit:literals) {
            cnf.clauses.push_back(conjunction?sat::Clause{-v,lit}:sat::Clause{v,-lit});
            clause.push_back(conjunction?-lit:lit);
        }
        cnf.clauses.push_back(std::move(clause));
        return v;
    }
    int comparison(const std::string&op,const Node&x,const Node&y){
        // Evaluate each term ONCE. Integer ite, div, mod can emit fresh
        // auxiliary arithmetic variables and guarded theory equalities.
        // Re-evaluating a term for each equality direction duplicates them.
        const std::string phrase="("+op+" "+text(x)+" "+text(y)+")";
        Linear left=term(x),right=term(y);
        if(op=="<=")return atom(linearLessEqual(left,right),phrase);
        if(op=="<")return atom(linearLessEqual(left,right,-1),phrase);
        if(op==">=")return atom(linearLessEqual(right,left),phrase);
        if(op==">")return atom(linearLessEqual(right,left,-1),phrase);
        if(op=="=")return linearEquality(left,right,phrase);
        if(op=="distinct")return combine(false,{
            atom(linearLessEqual(left,right,-1),phrase+" [<]"),
            atom(linearLessEqual(right,left,-1),phrase+" [>]")
        });
        throw Unsupported("Unsupported arithmetic relation");
    }
    int boolean(const Node&node) {
        if(!node.isList){
            if(node.text=="true")return constant(true);
            if(node.text=="false")return constant(false);
            if(const Binding*binding=bound(node.text)) {
                if(!binding->boolType)throw Unsupported("Expected Bool, found Int: "+node.text);
                return binding->booleanLiteral;
            }
            auto it=declaredBoolean.find(node.text);
            if(it!=declaredBoolean.end())return it->second;
            throw Unsupported("Unsupported Boolean symbol: "+node.text);
        }
        const std::string op=head(node);const size_t n=node.children.size();
        if(op=="let") {
            scopes.push_back(evaluatedLet(node));
            const int result=boolean(node.children[2]);
            scopes.pop_back();
            return result;
        }
        if(op=="and"||op=="or") {
            std::vector<int> args;
            for(size_t i=1;i<n;++i)args.push_back(boolean(node.children[i]));
            return combine(op=="and",args);
        }
        if(op=="not") {
            if(n!=2)throw Unsupported("not expects one argument");
            return -boolean(node.children[1]);
        }
        if(op=="=>") {
            if(n!=3)throw Unsupported("=> expects two arguments");
            return combine(false,{-boolean(node.children[1]),boolean(node.children[2])});
        }
        if(op=="xor") {
            if(n!=3)throw Unsupported("xor expects two arguments");
            int a=boolean(node.children[1]),b=boolean(node.children[2]);
            return combine(false,{combine(true,{a,-b}),combine(true,{-a,b})});
        }
        if(op=="ite") {
            if(n!=4)throw Unsupported("Boolean ite expects three arguments");
            if(!isBoolean(node.children[1])||!isBoolean(node.children[2])||
               !isBoolean(node.children[3]))
                throw Unsupported("Boolean ite requires three Boolean arguments");
            const int condition=boolean(node.children[1]);
            return combine(false,{
                combine(true,{condition,boolean(node.children[2])}),
                combine(true,{-condition,boolean(node.children[3])})
            });
        }
        if(op=="!") {
            if(n<2)throw Unsupported("Annotation needs a Boolean term");
            return boolean(node.children[1]);
        }
        if(op=="<="||op=="<"||op==">="||op==">"||op=="="||op=="distinct") {
            if(n<3)throw Unsupported("comparison requires at least two operands");
            bool anyBool=false;
            for(size_t i=1;i<n;++i)anyBool=anyBool||isBoolean(node.children[i]);
            if(anyBool) {
                if(op!="="&&op!="distinct")throw Unsupported("Ordered Boolean comparison");
                std::vector<int> arguments;
                for(size_t i=1;i<n;++i) {
                    if(!isBoolean(node.children[i]))
                        throw Unsupported("Cannot mix Bool and Int operands");
                    arguments.push_back(boolean(node.children[i]));
                }
                if(op=="distinct"&&arguments.size()>2)return constant(false);
                std::vector<int> pairwise;
                for(size_t i=1;i<arguments.size();++i) {
                    const int a=arguments[i-1],b=arguments[i];
                    const int equality=combine(false,{
                        combine(true,{a,b}),combine(true,{-a,-b})
                    });
                    pairwise.push_back(op=="="?equality:-equality);
                }
                return combine(true,pairwise);
            }
            std::vector<int> pairwise;
            if(op=="distinct") {
                for(size_t i=1;i<n;++i)for(size_t j=i+1;j<n;++j)
                    pairwise.push_back(comparison(op,node.children[i],node.children[j]));
            } else {
                for(size_t i=2;i<n;++i)
                    pairwise.push_back(comparison(op,node.children[i-1],node.children[i]));
            }
            return combine(true,pairwise);
        }
        throw Unsupported("Unsupported Boolean term "+op);
    }
};

struct RowChoice {int signedLiteral=0;Inequality row;int atomIndex=0;};
std::vector<RowChoice> theoryAssignment(const Builder&builder,const std::vector<int>&model) {
    std::vector<int>values(static_cast<size_t>(builder.cnf.variableCount)+1,0);
    for(int lit:model)values[static_cast<size_t>(std::abs(lit))]=lit>0?1:-1;
    std::vector<RowChoice>choices;
    for(size_t i=0;i<builder.atoms.size();++i) {
        const int v=builder.satVariables[i],signedLit=values[static_cast<size_t>(v)]>0?v:-v;
        Inequality row=builder.atoms[i];
        if(signedLit<0) {
            for(Int& a:row.coefficients)a=checkedNegate(a);
            row.rhs=checkedAdd(checkedNegate(row.rhs),-1);
        }
        choices.push_back({signedLit,std::move(row),static_cast<int>(i)});
    }
    return choices;
}
ArithmeticResult checkChoices(const std::vector<RowChoice>&choices,int n) {
    std::vector<Inequality> constraints;
    constraints.reserve(choices.size());
    for(const RowChoice&c:choices)constraints.push_back(c.row);
    return checkIntegers(constraints,n);
}
std::string jsonString(const std::string&s) {
    std::ostringstream output;output<<'"';
    for(unsigned char c:s) {
        if(c=='"'||c=='\\')output<<'\\'<<char(c);
        else if(c=='\n')output<<"\\n";
        else if(c=='\r')output<<"\\r";
        else if(c=='\t')output<<"\\t";
        else if(c<32)output<<"?";
        else output<<char(c);
    }
    return output.str()+'"';
}
} // namespace

std::string statusName(Status s) {
    switch(s) {case Status::Sat:return "SAT";case Status::Unsat:return "UNSAT";default:return "UNKNOWN";}
}
Result solveFile(const std::string&path) {
    Result result;
    try {
        std::ifstream in(path);
        if(!in)throw Unsupported("Cannot open SMT-LIB file: "+path);
        std::ostringstream input;input<<in.rdbuf();
        const std::vector<std::string> lex=tokens(input.str());
        size_t cursor=0;Terms commands;
        while(cursor<lex.size())commands.push_back(parseNode(lex,cursor));
        Builder builder(commands);
        result.atoms=builder.descriptions;
        sat::CnfFormula formula=builder.cnf;
        if(builder.atoms.size()>128)
            throw Unsupported("Current native QF_LIA Boolean abstraction supports <=128 theory atoms");
        for(int iteration=0;iteration<2048;++iteration) {
            ++result.booleanIterations;
            sat::Solver booleanSolver(formula);
            const sat::SolveStatus state=booleanSolver.solveCdcl();
            if(state==sat::SolveStatus::Unsatisfiable){
                result.status=Status::Unsat;
                result.reason="CDCL proved the Boolean abstraction and learned theory clauses inconsistent";
                return result;
            }
            std::vector<RowChoice>choices=theoryAssignment(builder,booleanSolver.model());
            ++result.theoryChecks;
            ArithmeticResult check=checkChoices(choices,builder.nvars);
            result.theoryBranches+=check.branches;
            if(check.state==ArithmeticResult::State::Sat) {
                result.status=Status::Sat;
                for(size_t i=0;i<builder.symbols.size();++i)result.model.emplace_back(
                    builder.symbols[static_cast<size_t>(i)],
                    printInt(check.model[static_cast<size_t>(i)]));
                std::vector<int> assignments(static_cast<size_t>(builder.cnf.variableCount)+1,0);
                for(const int literal:booleanSolver.model())
                    assignments[static_cast<size_t>(std::abs(literal))]=literal>0?1:-1;
                for(const auto&[name,var]:builder.declaredBoolean)
                    result.model.emplace_back(name,assignments[static_cast<size_t>(var)]>0?
                                                   "true":"false");
                return result;
            }
            if(check.state==ArithmeticResult::State::Unknown) {
                result.status=Status::Unknown;result.reason="LIA theory inconclusive: "+check.reason;
                return result;
            }
            // Every theory explanation must be sound: greedily minimize an
            // already PROVED-inconsistent set. If a recheck is UNKNOWN,
            // keep the atom rather than introducing an unproved lemma.
            if(choices.size()<=24)for(size_t index=0;index<choices.size();) {
                std::vector<RowChoice>tryWithout=choices;
                tryWithout.erase(tryWithout.begin()+static_cast<std::ptrdiff_t>(index));
                ++result.theoryChecks;
                ArithmeticResult test=checkChoices(tryWithout,builder.nvars);
                result.theoryBranches+=test.branches;
                if(test.state==ArithmeticResult::State::Unsat)choices=std::move(tryWithout);
                else ++index;
            }
            sat::Clause theoryLemma;
            for(const RowChoice&c:choices)theoryLemma.push_back(-c.signedLiteral);
            if(result.conflictAtoms.empty())for(const RowChoice&c:choices)
                result.conflictAtoms.push_back(c.atomIndex);
            formula.clauses.push_back(std::move(theoryLemma));
        }
        result.status=Status::Unknown;
        result.reason="Boolean/theory round budget exhausted";
    } catch(const Unsupported&error){
        result.status=Status::Unknown;result.reason=error.what();
    } catch(const std::overflow_error&error){
        result.status=Status::Unknown;result.reason=error.what();
    }
    return result;
}
std::string toJson(const Result&r) {
    std::ostringstream out;
    out<<"{\"status\":"<<jsonString(statusName(r.status))
       <<",\"backend\":\"Native C++ CDCL(T) QF_LIA\",\"reason\":"
       <<jsonString(r.reason)<<",\"boolean_iterations\":"<<r.booleanIterations
       <<",\"theory_checks\":"<<r.theoryChecks
       <<",\"theory_branches\":"<<r.theoryBranches
       <<",\"atoms\":[";
    for(size_t i=0;i<r.atoms.size();++i){
        if(i)out<<',';
        out<<jsonString(r.atoms[i]);
    }
    out<<"],\"conflict_atoms\":[";
    for(size_t i=0;i<r.conflictAtoms.size();++i){
        if(i)out<<',';
        out<<r.conflictAtoms[i];
    }
    out<<"],\"model\":{";
    for(size_t i=0;i<r.model.size();++i){
        if(i)out<<',';
        out<<jsonString(r.model[i].first)<<':'<<jsonString(r.model[i].second);
    }
    out<<"}}";
    return out.str();
}
} // namespace lia
