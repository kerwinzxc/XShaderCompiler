/*
 * AST.cpp
 * 
 * This file is part of the XShaderCompiler project (Copyright (c) 2014-2016 by Lukas Hermanns)
 * See "LICENSE.txt" for license information.
 */

#include "AST.h"
#include "Exception.h"
#include "HLSLIntrinsics.h"
#include "Variant.h"

#ifdef XSC_ENABLE_MEMORY_POOL
#include "MemoryPool.h"
#endif


namespace Xsc
{


/* ----- AST ----- */

AST::~AST()
{
    // dummy
}

#ifdef XSC_ENABLE_MEMORY_POOL

void* AST::operator new (std::size_t count)
{
    return MemoryPool::Instance().Alloc(count);
}

void AST::operator delete (void* ptr)
{
    MemoryPool::Instance().Free(ptr);
}

#endif


/* ----- TypedAST ----- */

const TypeDenoterPtr& TypedAST::GetTypeDenoter()
{
    if (!bufferedTypeDenoter_)
        bufferedTypeDenoter_ = DeriveTypeDenoter();
    return bufferedTypeDenoter_;
}

void TypedAST::ResetBufferedTypeDenoter()
{
    bufferedTypeDenoter_.reset();
}


/* ----- VarIdent ----- */

std::string VarIdent::ToString() const
{
    std::string name;
    auto ast = this;
    while (true)
    {
        name += ast->ident;
        if (ast->next)
        {
            ast = ast->next.get();
            name += ".";
        }
        else
            break;
    }
    return name;
}

VarIdent* VarIdent::LastVarIdent()
{
    return (next ? next->LastVarIdent() : this);
}

VarIdent* VarIdent::FirstConstVarIdent()
{
    if (symbolRef)
    {
        if (auto varDecl = symbolRef->As<VarDecl>())
        {
            if (varDecl->declStmntRef->IsConst())
                return this;
            if (next)
                return next->FirstConstVarIdent();
        }
    }
    return nullptr;
}

TypeDenoterPtr VarIdent::DeriveTypeDenoter()
{
    return GetExplicitTypeDenoter(true);
}

TypeDenoterPtr VarIdent::GetExplicitTypeDenoter(bool recursive)
{
    if (symbolRef)
    {
        /* Derive type denoter from symbol reference */
        switch (symbolRef->Type())
        {
            case AST::Types::VarDecl:
            {
                auto varDecl = static_cast<VarDecl*>(symbolRef);
                try
                {
                    return varDecl->GetTypeDenoter()->GetFromArray(arrayIndices.size(), (recursive ? next.get() : nullptr));
                }
                catch (const std::exception& e)
                {
                    RuntimeErr(e.what(), this);
                }
            }
            break;

            case AST::Types::BufferDecl:
            {
                auto bufferDecl = static_cast<BufferDecl*>(symbolRef);
                try
                {
                    return bufferDecl->GetTypeDenoter()->GetFromArray(arrayIndices.size(), (recursive ? next.get() : nullptr));
                }
                catch (const std::exception& e)
                {
                    RuntimeErr(e.what(), this);
                }
            }
            break;

            case AST::Types::SamplerDecl:
            {
                auto samplerDecl = static_cast<SamplerDecl*>(symbolRef);
                try
                {
                    return samplerDecl->GetTypeDenoter()->GetFromArray(arrayIndices.size(), (recursive ? next.get() : nullptr));
                }
                catch (const std::exception& e)
                {
                    RuntimeErr(e.what(), this);
                }
            }
            break;

            case AST::Types::StructDecl:
            {
                auto structDecl = static_cast<StructDecl*>(symbolRef);
                if (next)
                    RuntimeErr("can not directly access members of 'struct " + structDecl->SignatureToString() + "'", next.get());
                if (!arrayIndices.empty())
                    RuntimeErr("can not directly acces array of 'struct " + structDecl->SignatureToString() + "'", this);
                return structDecl->GetTypeDenoter()->Get();
            }
            break;

            case AST::Types::AliasDecl:
            {
                auto aliasDecl = static_cast<AliasDecl*>(symbolRef);
                if (next)
                    RuntimeErr("can not directly access members of '" + aliasDecl->ident + "'", next.get());
                if (!arrayIndices.empty())
                    RuntimeErr("can not directly access array of '" + aliasDecl->ident + "'", this);
                return aliasDecl->GetTypeDenoter()->Get();
            }
            break;
            
            default:
            {
                RuntimeErr("unknown type of symbol reference to derive type denoter of variable identifier '" + ident + "'", this);
            }
            break;
        }
    }
    RuntimeErr("missing symbol reference to derive type denoter of variable identifier '" + ident + "'", this);
}

void VarIdent::PopFront()
{
    if (next)
    {
        auto nextVarIdent = next;
        ident           = nextVarIdent->ident;
        arrayIndices    = nextVarIdent->arrayIndices;
        next            = nextVarIdent->next;
        symbolRef       = nextVarIdent->symbolRef;
    }
}


/* ----- SwitchCase ----- */

bool SwitchCase::IsDefaultCase() const
{
    return (expr == nullptr);
}


/* ----- Register ----- */

std::string Register::ToString() const
{
    std::string s;

    s += "Register(";
    
    if (registerType == RegisterType::Undefined)
        s += "<undefined>";
    else
        s += RegisterTypeToString(registerType);

    s += "[" + std::to_string(slot) + "])";

    return s;
}

Register* Register::GetForTarget(const std::vector<RegisterPtr>& registers, const ShaderTarget shaderTarget)
{
    for (const auto& slotRegister : registers)
    {
        if (slotRegister->shaderTarget == ShaderTarget::Undefined || slotRegister->shaderTarget == shaderTarget)
            return slotRegister.get();
    }
    return nullptr;
}


/* ----- PackOffset ----- */

std::string PackOffset::ToString() const
{
    std::string s;

    s += "PackOffset(";
    s += registerName;

    if (!vectorComponent.empty())
    {
        s += '.';
        s += vectorComponent;
    }

    s += ')';

    return s;
}


/* ----- VarType ----- */

std::string VarType::ToString() const
{
    return typeDenoter->ToString();
}


/* ----- VarDecl ----- */

std::string VarDecl::ToString() const
{
    std::string s;

    s += ident;

    for (std::size_t i = 0; i < arrayDims.size(); ++i)
        s += "[]";

    if (semantic != Semantic::Undefined)
    {
        s += " : ";
        s += semantic.ToString();
    }

    if (initializer)
    {
        s += " = ";
        //TODO: see above
        s += "???";
        //s += initializer->ToString();
    }

    return s;
}

TypeDenoterPtr VarDecl::DeriveTypeDenoter()
{
    if (declStmntRef)
    {
        /* Get base type denoter from declaration statement */
        return declStmntRef->varType->typeDenoter->AsArray(arrayDims);
    }
    RuntimeErr("missing reference to declaration statement to derive type denoter of variable identifier '" + ident + "'", this);
}


/* ----- BufferDecl ----- */

TypeDenoterPtr BufferDecl::DeriveTypeDenoter()
{
    return std::make_shared<BufferTypeDenoter>(this)->AsArray(arrayDims);
}

BufferType BufferDecl::GetBufferType() const
{
    return (declStmntRef ? declStmntRef->typeDenoter->bufferType : BufferType::Undefined);
}


/* ----- SamplerDecl ----- */

TypeDenoterPtr SamplerDecl::DeriveTypeDenoter()
{
    return std::make_shared<SamplerTypeDenoter>(this)->AsArray(arrayDims);
}

SamplerType SamplerDecl::GetSamplerType() const
{
    return (declStmntRef ? declStmntRef->typeDenoter->samplerType : SamplerType::Undefined);
}


/* ----- StructDecl ----- */

std::string StructDecl::SignatureToString() const
{
    return ("struct " + (IsAnonymous() ? "<anonymous>" : ident));
}

bool StructDecl::IsAnonymous() const
{
    return ident.empty();
}

VarDecl* StructDecl::Fetch(const std::string& ident) const
{
    /* Fetch symbol from base struct first */
    if (baseStructRef)
    {
        auto varDecl = baseStructRef->Fetch(ident);
        if (varDecl)
            return varDecl;
    }

    /* Now fetch symbol from members */
    for (const auto& varDeclStmnt : members)
    {
        auto symbol = varDeclStmnt->Fetch(ident);
        if (symbol)
            return symbol;
    }

    return nullptr;
}

TypeDenoterPtr StructDecl::DeriveTypeDenoter()
{
    return std::make_shared<StructTypeDenoter>(this);
}

bool StructDecl::HasNonSystemValueMembers() const
{
    /* Check if base structure has any non-system-value members */
    if (baseStructRef && baseStructRef->HasNonSystemValueMembers())
        return true;
    
    /* Search for non-system-value member */
    for (const auto& member : members)
    {
        for (const auto& varDecl : member->varDecls)
        {
            if (!varDecl->semantic.IsSystemValue())
                return true;
        }
    }

    /* No non-system-value member found */
    return false;
}

std::size_t StructDecl::NumMembers() const
{
    std::size_t n = 0;

    if (baseStructRef)
        n += baseStructRef->NumMembers();

    for (const auto& member : members)
        n += member->varDecls.size();

    return n;
}

void StructDecl::CollectMemberTypeDenoters(std::vector<TypeDenoterPtr>& memberTypeDens) const
{
    /* First collect type denoters from base structure */
    if (baseStructRef)
        baseStructRef->CollectMemberTypeDenoters(memberTypeDens);

    /* Collect type denoters from this structure */
    for (const auto& member : members)
    {
        /* Add type denoter N times (where N is the number variable declaration within the member statement) */
        for (std::size_t i = 0; i < member->varDecls.size(); ++i)
            memberTypeDens.push_back(member->varType->typeDenoter);
    }
}


/* ----- AliasDecl ----- */

TypeDenoterPtr AliasDecl::DeriveTypeDenoter()
{
    return typeDenoter;
}


/* ----- FunctionDecl ----- */

void FunctionDecl::ParameterSemantics::Add(VarDecl* varDecl)
{
    if (varDecl)
    {
        if (varDecl->flags(VarDecl::isSystemValue))
            varDeclRefsSV.push_back(varDecl);
        else
            varDeclRefs.push_back(varDecl);
    }
}

bool FunctionDecl::IsForwardDecl() const
{
    return (codeBlock == nullptr);
}

bool FunctionDecl::HasVoidReturnType() const
{
    return returnType->typeDenoter->IsVoid();
}

std::string FunctionDecl::SignatureToString(bool useParamNames) const
{
    std::string s;

    s += returnType->ToString();
    s += ' ';
    s += ident;
    s += '(';

    for (std::size_t i = 0; i < parameters.size(); ++i)
    {
        s += parameters[i]->ToString(useParamNames);
        if (i + 1 < parameters.size())
            s += ", ";
    }

    s += ')';

    return s;
}

bool FunctionDecl::EqualsSignature(const FunctionDecl& rhs) const
{
    /* Compare parameter count */
    if (parameters.size() != rhs.parameters.size())
        return false;

    /* Compare parameter type denoters */
    for (std::size_t i = 0; i < parameters.size(); ++i)
    {
        auto lhsTypeDen = parameters[i]->varType->typeDenoter.get();
        auto rhsTypeDen = rhs.parameters[i]->varType->typeDenoter.get();

        if (!lhsTypeDen->Equals(*rhsTypeDen))
            return false;
    }

    return true;
}

std::size_t FunctionDecl::NumMinArgs() const
{
    std::size_t n = 0;

    for (const auto& param : parameters)
    {
        if (!param->varDecls.empty() && param->varDecls.front()->initializer != nullptr)
            break;
        ++n;
    }

    return n;
}

std::size_t FunctionDecl::NumMaxArgs() const
{
    return parameters.size();
}

bool FunctionDecl::MatchParameterWithTypeDenoter(std::size_t paramIndex, const TypeDenoter& argType, bool implicitConversion) const
{
    if (paramIndex >= parameters.size())
        return false;

    /* Get type denoters to compare */
    auto paramTypeDen = parameters[paramIndex]->varType->typeDenoter.get();

    /* Check for explicit compatability: are they equal? */
    if (!argType.Equals(*paramTypeDen))
    {
        if (implicitConversion)
        {
            /* Check for implicit compatability: is it castable? */
            if (!argType.IsCastableTo(*paramTypeDen))
                return false;
        }
        else
            return false;
    }

    return true;
}


/* ----- UniformBufferDecl ----- */

std::string UniformBufferDecl::ToString() const
{
    std::string s;

    switch (bufferType)
    {
        case UniformBufferType::Undefined:
            s = "<undefined buffer> ";
            break;
        case UniformBufferType::ConstantBuffer:
            s = "cbuffer ";
            break;
        case UniformBufferType::TextureBuffer:
            s = "tbuffer ";
            break;
    }

    s += ident;

    return s;
}


/* ----- VarDelcStmnt ----- */

std::string VarDeclStmnt::ToString(bool useVarNames) const
{
    auto s = varType->ToString();
    
    if (useVarNames)
    {
        for (std::size_t i = 0; i < varDecls.size(); ++i)
        {
            s += ' ';
            s += varDecls[i]->ToString();
            if (i + 1 < varDecls.size())
                s += ',';
        }
    }

    return s;
}

VarDecl* VarDeclStmnt::Fetch(const std::string& ident) const
{
    for (const auto& var : varDecls)
    {
        if (var->ident == ident)
            return var.get();
    }
    return nullptr;
}

bool VarDeclStmnt::IsInput() const
{
    return (isInput || !isOutput);
}

bool VarDeclStmnt::IsOutput() const
{
    return isOutput;
}

bool VarDeclStmnt::IsConst() const
{
    return (isUniform || (typeModifiers.find(TypeModifier::Const) != typeModifiers.end()));
}

bool VarDeclStmnt::HasAnyTypeModifierOf(const std::vector<TypeModifier>& modifiers) const
{
    for (auto mod : modifiers)
    {
        if (typeModifiers.find(mod) != typeModifiers.end())
            return true;
    }
    return false;
}


/* ----- NullExpr ----- */

TypeDenoterPtr NullExpr::DeriveTypeDenoter()
{
    /*
    Return 'int' as type, because null expressions are only
    used as dynamic array dimensions (which must be integral types)
    */
    return std::make_shared<BaseTypeDenoter>(DataType::Int);
}


/* ----- ListExpr ----- */

TypeDenoterPtr ListExpr::DeriveTypeDenoter()
{
    /* Only return type denoter of first sub expression */
    return firstExpr->GetTypeDenoter();
}


/* ----- LiteralExpr ----- */

TypeDenoterPtr LiteralExpr::DeriveTypeDenoter()
{
    return std::make_shared<BaseTypeDenoter>(dataType);
}

void LiteralExpr::ConvertDataType(const DataType type)
{
    if (dataType != type)
    {
        /* Parse variant from value string */
        auto variant = Variant::ParseFrom(value);

        switch (type)
        {
            case DataType::Bool:
                variant.ToBool();
                value = variant.ToString();
                break;

            case DataType::Int:
                variant.ToInt();
                value = variant.ToString();
                break;

            case DataType::UInt:
                variant.ToInt();
                value = variant.ToString() + "u";
                break;

            case DataType::Half:
            case DataType::Float:
            case DataType::Double:
                variant.ToReal();
                value = variant.ToString();
                break;
                
            default:
                break;
        }

        /* Set new data type and reset buffered type dentoer */
        dataType = type;
        ResetBufferedTypeDenoter();
    }
}

std::string LiteralExpr::GetStringValue() const
{
    /* Return string literal content (without the quotation marks) */
    if (dataType == DataType::String && value.size() >= 2 && value.front() == '\"' && value.back() == '\"')
        return value.substr(1, value.size() - 2);
    else
        return "";
}


/* ----- TypeNameExpr ----- */

TypeDenoterPtr TypeNameExpr::DeriveTypeDenoter()
{
    return typeDenoter;
}


/* ----- TernaryExpr ----- */

TypeDenoterPtr TernaryExpr::DeriveTypeDenoter()
{
    /* Check if conditional expression is compatible to a boolean */
    const auto& condTypeDen = condExpr->GetTypeDenoter();
    const BaseTypeDenoter boolTypeDen(DataType::Bool);

    if (!condTypeDen->IsCastableTo(boolTypeDen))
    {
        RuntimeErr(
            "can not cast '" + condTypeDen->ToString() + "' to '" +
            boolTypeDen.ToString() + "' in condition of ternary expression", condExpr.get()
        );
    }

    /* Return type of 'then'-branch sub expresion if the types are compatible */
    const auto& thenTypeDen = thenExpr->GetTypeDenoter();
    const auto& elseTypeDen = elseExpr->GetTypeDenoter();

    if (!elseTypeDen->IsCastableTo(*thenTypeDen))
    {
        RuntimeErr(
            "can not cast '" + elseTypeDen->ToString() + "' to '" +
            thenTypeDen->ToString() + "' in ternary expression", this
        );
    }

    return thenTypeDen;
}


/* ----- BinaryExpr ----- */

TypeDenoterPtr BinaryExpr::DeriveTypeDenoter()
{
    /* Return type of left-hand-side sub expresion if the types are compatible */
    const auto& lhsTypeDen = lhsExpr->GetTypeDenoter();
    const auto& rhsTypeDen = rhsExpr->GetTypeDenoter();

    if (!rhsTypeDen->IsCastableTo(*lhsTypeDen) || !lhsTypeDen->IsCastableTo(*rhsTypeDen))
    {
        RuntimeErr(
            "can not cast '" + rhsTypeDen->ToString() + "' to '" + lhsTypeDen->ToString() +
            "' in binary expression '" + BinaryOpToString(op) + "'", this
        );
    }

    if (IsBooleanOp(op))
        return std::make_shared<BaseTypeDenoter>(DataType::Bool);
    else
        return lhsTypeDen;
}


/* ----- UnaryExpr ----- */

TypeDenoterPtr UnaryExpr::DeriveTypeDenoter()
{
    const auto& typeDen = expr->GetTypeDenoter();

    if (IsLogicalOp(op))
        return std::make_shared<BaseTypeDenoter>(DataType::Bool);
    else
        return typeDen;
}


/* ----- PostUnaryExpr ----- */

TypeDenoterPtr PostUnaryExpr::DeriveTypeDenoter()
{
    return expr->GetTypeDenoter();
}


/* ----- FunctionCallExpr ----- */

TypeDenoterPtr FunctionCallExpr::DeriveTypeDenoter()
{
    if (call->funcDeclRef)
        return call->funcDeclRef->returnType->typeDenoter;
    else if (call->typeDenoter)
        return call->typeDenoter;
    else if (call->intrinsic != Intrinsic::Undefined)
    {
        try
        {
            return GetTypeDenoterForHLSLIntrinsicWithArgs(call->intrinsic, call->arguments);
        }
        catch (const std::exception& e)
        {
            RuntimeErr(e.what(), this);
        }
    }
    else
        RuntimeErr("missing function reference to derive expression type", this);
}


/* ----- BracketExpr ----- */

TypeDenoterPtr BracketExpr::DeriveTypeDenoter()
{
    return expr->GetTypeDenoter();
}


/* ----- SuffixExpr ----- */

TypeDenoterPtr SuffixExpr::DeriveTypeDenoter()
{
    return expr->GetTypeDenoter()->Get(varIdent.get());
}


/* ----- ArrayAccessExpr ----- */

TypeDenoterPtr ArrayAccessExpr::DeriveTypeDenoter()
{
    try
    {
        return expr->GetTypeDenoter()->GetFromArray(arrayIndices.size());
    }
    catch (const std::exception& e)
    {
        RuntimeErr(e.what(), this);
    }
}


/* ----- CastExpr ----- */

TypeDenoterPtr CastExpr::DeriveTypeDenoter()
{
    const auto& castTypeDen = typeExpr->GetTypeDenoter();
    const auto& valueTypeDen = expr->GetTypeDenoter();

    if (!valueTypeDen->IsCastableTo(*castTypeDen))
    {
        RuntimeErr(
            "can not cast '" + valueTypeDen->ToString() + "' to '" +
            castTypeDen->ToString() + "' in cast expression", this
        );
    }

    return castTypeDen;
}


/* ----- VarAccessExpr ----- */

TypeDenoterPtr VarAccessExpr::DeriveTypeDenoter()
{
    return varIdent->GetTypeDenoter();
}


/* ----- InitializerExpr ----- */

TypeDenoterPtr InitializerExpr::DeriveTypeDenoter()
{
    if (exprs.empty())
        RuntimeErr("can not derive type of initializer list with no elements", this);
    return std::make_shared<ArrayTypeDenoter>(exprs.front()->GetTypeDenoter(), std::vector<ExprPtr>{ nullptr });
}

unsigned int InitializerExpr::NumElements() const
{
    unsigned int n = 0;
    
    for (const auto& e : exprs)
    {
        if (e->Type() == AST::Types::InitializerExpr)
            n += static_cast<const InitializerExpr&>(*e).NumElements();
        else
            ++n;
    }

    return n;
}


} // /namespace Xsc



// ================================================================================
