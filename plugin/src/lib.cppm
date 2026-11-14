module;
#include <clang/AST/ASTConsumer.h>
#include <clang/AST/Attr.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/ExprCXX.h>
#include <clang/AST/ExprConcepts.h>
#include <clang/AST/Mangle.h>
#include <clang/AST/StmtCXX.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Sema/Lookup.h>
#include <clang/Sema/Sema.h>

export module vui.compose.plugin;
import std;

namespace vui_compose {
constexpr llvm::StringLiteral compose_marker = "vui.compose.v2";
constexpr llvm::StringLiteral content_marker = "vui.compose.content.v2";
constexpr llvm::StringLiteral hook_marker = "vui.compose.hook.v2";

auto annotation(const clang::Decl &declaration, llvm::StringRef name) -> bool {
    for (const auto *attribute : declaration.specific_attrs<clang::AnnotateAttr>())
        if (attribute->getAnnotation() == name) return true;
    return false;
}
auto marked(const clang::FunctionDecl &function, llvm::StringRef name) -> bool {
    for (const auto *declaration : function.redecls())
        if (annotation(*declaration, name)) return true;
    if (auto *pattern = function.getTemplateInstantiationPattern())
        if (pattern != &function) return marked(*pattern, name);
    return false;
}
auto parameter_annotation(const clang::FunctionDecl &function, unsigned index, llvm::StringRef marker) -> bool {
    for (const auto *declaration : function.redecls())
        if (index < declaration->getNumParams() && annotation(*declaration->getParamDecl(index), marker)) return true;
    if (auto *pattern = function.getTemplateInstantiationPattern())
        if (pattern != &function) return parameter_annotation(*pattern, index, marker);
    return false;
}
auto content_parameter(const clang::FunctionDecl &function, unsigned index) -> bool {
    return parameter_annotation(function, index, content_marker);
}
auto type_marker(clang::QualType type, llvm::StringRef marker) -> bool {
    auto *record = type.getCanonicalType()->getAsCXXRecordDecl();
    if (record)
        for (auto *declaration : record->redecls())
            if (annotation(*declaration, marker)) return true;
    if (auto *specialization = llvm::dyn_cast_or_null<clang::ClassTemplateSpecializationDecl>(record))
        return annotation(*specialization->getSpecializedTemplate()->getTemplatedDecl(), marker);
    return false;
}
auto element_result(clang::QualType type) -> bool {
    if (type_marker(type, "vui.compose.element.v1")) return true;
    auto *result = llvm::dyn_cast_or_null<clang::ClassTemplateSpecializationDecl>(type.getCanonicalType()->getAsCXXRecordDecl());
    if (!result || result->getTemplateArgs().size() != 2) return false;
    auto &value = result->getTemplateArgs().get(0);
    auto &error = result->getTemplateArgs().get(1);
    return value.getKind() == clang::TemplateArgument::Type && error.getKind() == clang::TemplateArgument::Type &&
        type_marker(value.getAsType(), "vui.compose.element.v1") && type_marker(error.getAsType(), "vui.compose.error.v1");
}
void diagnose(clang::CompilerInstance &compiler, clang::SourceLocation location, llvm::StringRef message) {
    auto id = compiler.getDiagnostics().getCustomDiagID(clang::DiagnosticsEngine::Error, "%0");
    compiler.getDiagnostics().Report(location, id) << message;
}
auto hash(llvm::StringRef text, std::uint64_t value = 14695981039346656037ull) -> std::uint64_t {
    for (unsigned char byte : text) value = (value ^ byte) * 1099511628211ull;
    return value ? value : 1;
}

class Consumer final : public clang::ASTConsumer {
    clang::CompilerInstance &compiler_;
    std::set<const clang::FunctionDecl *> processed_;
    clang::FunctionTemplateDecl *call_{};
    clang::FunctionTemplateDecl *invoke_{};
    clang::FunctionDecl *source_{};
    struct Region {
        std::uint64_t identity;
        unsigned next{};
        bool compose{}, context{};
    };
    auto runtime_owner(const clang::FunctionDecl &function) -> bool {
        for (auto *owner = function.getDeclContext(); owner; owner = owner->getParent())
            if (auto *space = llvm::dyn_cast<clang::NamespaceDecl>(owner))
                for (auto *declaration : space->redecls())
                    if (annotation(*declaration, "vui.compose.runtime.v1")) return true;
        return false;
    }
    auto compatible(const clang::FunctionDecl &function) -> bool {
        for (auto *declaration : function.redecls()) {
            for (auto *attribute : declaration->specific_attrs<clang::AnnotateAttr>()) {
                auto value = attribute->getAnnotation();
                if (value.starts_with("vui.compose.v") && value != compose_marker) {
                    diagnose(compiler_, declaration->getLocation(), "incompatible compose declaration protocol; rebuild its producer with this plugin");
                    return false;
                }
            }
            for (auto *parameter : declaration->parameters())
                for (auto *attribute : parameter->specific_attrs<clang::AnnotateAttr>()) {
                    auto value = attribute->getAnnotation();
                    if (value.starts_with("vui.compose.content.v") && value != content_marker) {
                        diagnose(compiler_, parameter->getLocation(), "incompatible compose content protocol; rebuild its producer with this plugin");
                        return false;
                    }
                    if (value.starts_with("vui.compose.deferred.v") && value != "vui.compose.deferred.v2") {
                        diagnose(compiler_, parameter->getLocation(), "incompatible deferred callback protocol; rebuild its producer with this plugin");
                        return false;
                    }
                }
        }
        if (auto *pattern = function.getTemplateInstantiationPattern(); pattern && pattern != &function) return compatible(*pattern);
        return true;
    }
    auto owned_captures(const clang::CXXRecordDecl &closure) -> bool {
        for (const auto &capture : closure.captures()) {
            if (capture.getCaptureKind() == clang::LCK_ByRef || capture.getCaptureKind() == clang::LCK_This) {
                diagnose(compiler_, capture.getLocation(), "deferred compose callbacks require value captures; capture an owning handle instead of a reference or this");
                return false;
            }
            if (!capture.capturesVariable()) continue;
            auto type = capture.getCapturedVar()->getType().getNonReferenceType();
            bool borrowed = type->isPointerType();
            if (auto *record = llvm::dyn_cast_or_null<clang::ClassTemplateSpecializationDecl>(type->getAsCXXRecordDecl())) {
                auto name = record->getSpecializedTemplate()->getQualifiedNameAsString();
                borrowed |= name == "rstd::ref" || name == "rstd::mut_ref" || name == "rstd::ptr" || name == "rstd::mut_ptr";
            }
            if (borrowed) {
                diagnose(compiler_, capture.getLocation(), "compose captures require owning values or an explicit Borrowed handle instead of a pointer or view");
                return false;
            }
        }
        return true;
    }
    auto intrinsic(llvm::StringRef name, llvm::StringRef marker) -> clang::NamedDecl * {
        auto &sema = compiler_.getSema();
        clang::DeclContext *owner = compiler_.getASTContext().getTranslationUnitDecl();
        for (auto part : { llvm::StringRef("vui"), llvm::StringRef("compose"), llvm::StringRef("compiler") }) {
            clang::LookupResult lookup(sema, &sema.Context.Idents.get(part), {}, clang::Sema::LookupOrdinaryName);
            if (!sema.LookupQualifiedName(lookup, owner) || !lookup.isSingleResult()) return nullptr;
            owner = llvm::dyn_cast<clang::NamespaceDecl>(lookup.getFoundDecl()->getUnderlyingDecl());
            if (!owner) return nullptr;
        }
        clang::LookupResult lookup(sema, &sema.Context.Idents.get(name), {}, clang::Sema::LookupOrdinaryName);
        if (!sema.LookupQualifiedName(lookup, owner)) return nullptr;
        for (auto *named : lookup) {
            if (auto *function = llvm::dyn_cast<clang::FunctionTemplateDecl>(named->getUnderlyingDecl())) {
                if (marked(*function->getTemplatedDecl(), marker)) return function;
            } else if (auto *function = llvm::dyn_cast<clang::FunctionDecl>(named->getUnderlyingDecl())) {
                if (marked(*function, marker)) return function;
            }
        }
        return nullptr;
    }
    auto bridge(llvm::StringRef name, llvm::StringRef marker) -> clang::FunctionTemplateDecl * {
        return llvm::dyn_cast_or_null<clang::FunctionTemplateDecl>(intrinsic(name, marker));
    }
    auto integer(std::uint64_t value, clang::QualType type, clang::SourceLocation location) -> clang::Expr * {
        auto &context = compiler_.getASTContext();
        return clang::IntegerLiteral::Create(context, llvm::APInt(context.getTypeSize(type), value), type, location);
    }
    auto site(Region &region) -> std::uint64_t { return hash(std::to_string(++region.next), region.identity); }
    auto call_site(Region &region, clang::SourceLocation location) -> clang::Expr * {
        if (!source_) source_ = llvm::dyn_cast_or_null<clang::FunctionDecl>(intrinsic("source", "vui.compose.source.v2"));
        if (!source_) {
            diagnose(compiler_, location, "compose requires the compatible CallSite source bridge; import vui.compose");
            return nullptr;
        }
        auto &sema = compiler_.getSema();
        auto &context = sema.Context;
        auto origin = compiler_.getSourceManager().getPresumedLoc(location);
        auto literal = [&](llvm::StringRef text) -> clang::Expr * {
            auto type = context.getConstantArrayType(context.CharTy.withConst(), llvm::APInt(64, text.size() + 1), nullptr, clang::ArraySizeModifier::Normal, 0);
            return clang::StringLiteral::Create(context, text, clang::StringLiteralKind::Ordinary, false, type, {location});
        };
        auto *reference = new (context) clang::DeclRefExpr(context, source_, false, source_->getType(), clang::VK_LValue, location);
        llvm::SmallVector<clang::Expr *> arguments{
            integer(site(region), source_->getParamDecl(0)->getType(), location),
            literal(origin.isValid() ? origin.getFilename() : ""),
            literal(sema.CurContext->isFunctionOrMethod() ? clang::Decl::castFromDeclContext(sema.CurContext)->getDeclKindName() : "compose"),
            integer(origin.isValid() ? origin.getLine() : 0, context.UnsignedIntTy, location),
            integer(origin.isValid() ? origin.getColumn() : 0, context.UnsignedIntTy, location)};
        if (auto *function = llvm::dyn_cast<clang::FunctionDecl>(clang::Decl::castFromDeclContext(sema.CurContext)))
            arguments[2] = literal(function->getQualifiedNameAsString());
        auto result = sema.BuildCallExpr(nullptr, reference, location, arguments, location);
        if (result.isInvalid()) return nullptr;
        clang::Expr::EvalResult value;
        if (!result.get()->EvaluateAsRValue(value, context) || value.HasSideEffects) {
            diagnose(compiler_, location, "compose CallSite must be a compile-time constant");
            return nullptr;
        }
        return clang::ConstantExpr::Create(context, result.get(), value.Val);
    }
    auto lambda(clang::Expr *expression) -> clang::LambdaExpr * {
        expression = expression->IgnoreParenImpCasts();
        if (auto *value = llvm::dyn_cast<clang::LambdaExpr>(expression)) return value;
        if (auto *value = llvm::dyn_cast<clang::ExprWithCleanups>(expression)) return lambda(value->getSubExpr());
        if (auto *value = llvm::dyn_cast<clang::MaterializeTemporaryExpr>(expression)) return lambda(value->getSubExpr());
        if (auto *value = llvm::dyn_cast<clang::CXXBindTemporaryExpr>(expression)) return lambda(value->getSubExpr());
        if (auto *value = llvm::dyn_cast<clang::CXXConstructExpr>(expression); value && value->getNumArgs() == 1) return lambda(value->getArg(0));
        return nullptr;
    }
    void lambda_body(clang::LambdaExpr &expression, Region &parent, bool content) {
        auto *function = expression.getCallOperator();
        bool composed = content || marked(*function, compose_marker);
        if (composed && !owned_captures(*expression.getLambdaClass())) return;
        if (content && !marked(*function, compose_marker))
            function->addAttr(clang::AnnotateAttr::Create(compiler_.getASTContext(), compose_marker, nullptr, 0, function->getSourceRange()));
        Region nested{site(parent), 0, composed, composed};
        if (!function->isDependentContext() && processed_.insert(function).second) {
            clang::Sema::ContextRAII scope(compiler_.getSema(), function);
            function->setBody(transform(function->getBody(), nested));
        }
        if (auto *pattern = function->getDescribedFunctionTemplate())
            for (auto *specialization : pattern->specializations()) process(*specialization);
    }
    auto lower(clang::CallExpr &original, clang::FunctionDecl &function, Region &region) -> clang::Expr * {
        auto location = original.getExprLoc();
        if (!element_result(function.getReturnType())) {
            diagnose(compiler_, location, "compose function must return Element or ComposeResult<Element>");
            return &original;
        }
        if (!region.compose) {
            diagnose(compiler_, location, "compose call in an ordinary lambda requires a content parameter or [[vui::compose]]");
            return &original;
        }
        auto *method = llvm::dyn_cast<clang::CXXMethodDecl>(&function);
        bool closure = method && method->getParent()->isLambda();
        if (method && !method->isStatic() && !closure) {
            diagnose(compiler_, location, "compose methods require a static function or a lambda with owned captures");
            return &original;
        }
        if (!closure && original.getCallee()->HasSideEffects(compiler_.getASTContext())) {
            diagnose(compiler_, location, "compose calls require a callee expression without side effects; use a qualified function name");
            return &original;
        }
        if (closure) {
            auto *operation = llvm::dyn_cast<clang::CXXOperatorCallExpr>(&original);
            if (operation && operation->getNumArgs() > 1 && operation->getArg(0)->HasSideEffects(compiler_.getASTContext())) {
                diagnose(compiler_, location, "evaluate the compose callable before its arguments to preserve C++ evaluation order");
                return &original;
            }
        }
        auto *&helper = closure ? invoke_ : call_;
        if (!helper) helper = bridge(closure ? "invoke" : "call", closure ? "vui.compose.invoke.v2" : "vui.compose.call.v2");
        if (!helper) {
            diagnose(compiler_, location, "compose call requires the compatible vui.compose compiler bridge; import vui.compose");
            return &original;
        }
        auto &sema = compiler_.getSema();
        auto &context = sema.Context;
        clang::TemplateArgumentListInfo templates(location, location);
        if (!closure) {
            auto *reference = new (context) clang::DeclRefExpr(context, &function, false, function.getType(), clang::VK_LValue, location);
            auto address = sema.BuildUnaryOp(nullptr, location, clang::UO_AddrOf, reference);
            if (address.isInvalid()) return &original;
            templates.addArgument(clang::TemplateArgumentLoc(clang::TemplateArgument(&function, address.get()->getType()), address.get()));
        }
        clang::LookupResult lookup(sema, helper->getDeclName(), location, clang::Sema::LookupOrdinaryName);
        lookup.addDecl(helper);
        lookup.resolveKind();
        clang::CXXScopeSpec scope;
        auto reference = sema.BuildTemplateIdExpr(scope, {}, lookup, false, closure ? nullptr : &templates);
        if (reference.isInvalid()) return &original;
        llvm::SmallVector<clang::Expr *> arguments;
        auto *origin = call_site(region, location);
        if (!origin) return &original;
        arguments.push_back(origin);
        unsigned first = 0;
        if (closure) {
            auto *operation = llvm::dyn_cast<clang::CXXOperatorCallExpr>(&original);
            if (!operation || !operation->getNumArgs()) {
                diagnose(compiler_, location, "compose lambda requires a direct call operator invocation");
                return &original;
            }
            arguments.push_back(operation->getArg(0));
            first = 1;
        }
        for (unsigned index = first; index < original.getNumArgs(); ++index) arguments.push_back(original.getArg(index));
        auto invocation = sema.BuildCallExpr(nullptr, reference.get(), location, arguments, original.getRParenLoc());
        if (invocation.isInvalid()) return &original;
        if (!context.hasSameType(invocation.get()->getType(), original.getType())) {
            diagnose(compiler_, location, "compose compiler bridge changed the source result type");
            return &original;
        }
        return invocation.get();
    }
    auto transform(clang::Stmt *statement, Region &region) -> clang::Stmt * {
        if (!statement) return nullptr;
        if (llvm::isa<clang::UnaryExprOrTypeTraitExpr, clang::CXXNoexceptExpr, clang::RequiresExpr>(statement)) return statement;
        if (auto *construction = llvm::dyn_cast<clang::CXXConstructExpr>(statement))
            for (unsigned index = 0; index < construction->getNumArgs(); ++index)
                if (content_parameter(*construction->getConstructor(), index))
                    if (auto *expression = lambda(construction->getArg(index))) lambda_body(*expression, region, true);
        if (auto *expression = llvm::dyn_cast<clang::LambdaExpr>(statement)) {
            for (auto *&initializer : expression->capture_inits())
                if (initializer) initializer = llvm::cast<clang::Expr>(transform(initializer, region));
            lambda_body(*expression, region, false);
            return statement;
        }
        if (auto *reference = llvm::dyn_cast<clang::DeclRefExpr>(statement))
            if (auto *function = llvm::dyn_cast<clang::FunctionDecl>(reference->getDecl()); function && marked(*function, compose_marker)) {
                diagnose(compiler_, reference->getExprLoc(), "compose functions require direct calls; ordinary function pointers do not preserve the composition protocol");
                return statement;
            }
        if (auto *call = llvm::dyn_cast<clang::CallExpr>(statement)) {
            auto *callee = call->getDirectCallee();
            if (callee && !compatible(*callee)) return statement;
            auto *method = llvm::dyn_cast_or_null<clang::CXXMethodDecl>(callee);
            unsigned first = llvm::isa<clang::CXXOperatorCallExpr>(call) && method && !method->isStatic() ? 1 : 0;
            for (unsigned index = first; index < call->getNumArgs(); ++index)
                if (callee && content_parameter(*callee, index - first))
                    if (auto *expression = lambda(call->getArg(index))) lambda_body(*expression, region, true);
            for (unsigned index = first; callee && index < call->getNumArgs(); ++index)
                if (parameter_annotation(*callee, index - first, "vui.compose.deferred.v2")) {
                    auto type = call->getArg(index)->getType().getNonReferenceType();
                    if (auto *closure = type->getAsCXXRecordDecl(); closure && closure->isLambda())
                        owned_captures(*closure);
                }
            for (auto child = statement->child_begin(); child != statement->child_end(); ++child)
                if (*child && !(*child == call->getCallee() && callee && marked(*callee, compose_marker)))
                    *child = transform(*child, region);
            if (!callee || call->isTypeDependent() || call->isValueDependent()) return statement;
            bool content_call = marked(*callee, "vui.compose.content.call.v2");
            if (marked(*callee, hook_marker) || content_call) {
                if (!content_call && !region.context) {
                    diagnose(compiler_, call->getExprLoc(), "compose hooks require a compose function or content lambda");
                    return statement;
                }
                if (!call->getNumArgs()) return statement;
                auto last = call->getNumArgs() - 1;
                if (llvm::isa<clang::CXXDefaultArgExpr>(call->getArg(last))) {
                    if (auto *origin = call_site(region, call->getExprLoc())) call->setArg(last, origin);
                }
            } else if (marked(*callee, compose_marker)) return lower(*call, *callee, region);
            return statement;
        }
        for (auto child = statement->child_begin(); child != statement->child_end(); ++child)
            if (*child) *child = transform(*child, region);
        return statement;
    }
    void process(clang::FunctionDecl &function) {
        if (!function.doesThisDeclarationHaveABody() || function.isDependentContext()) return;
        if (!processed_.insert(&function).second) return;
        if (runtime_owner(function)) return;
        if (marked(function, "vui.compose.call.v2") || marked(function, "vui.compose.invoke.v2") || marked(function, hook_marker) || marked(function, "vui.compose.content.call.v2")) return;
        if (compiler_.getSourceManager().isInSystemHeader(function.getLocation())) return;
        bool compose = marked(function, compose_marker);
        bool content = false;
        for (unsigned index = 0; index < function.getNumParams(); ++index) content |= content_parameter(function, index);
        if (function.isFromASTFile() && (!function.isTemplateInstantiation() || (!compose && !content))) return;
        if (!compatible(function)) return;
        for (unsigned index = 0; index < function.getNumParams(); ++index)
            if (content_parameter(function, index)) {
                auto *closure = function.getParamDecl(index)->getType().getNonReferenceType()->getAsCXXRecordDecl();
                if (type_marker(function.getParamDecl(index)->getType().getNonReferenceType(), "vui.compose.content.type.v2")) continue;
                if (!closure || !closure->isLambda()) {
                    diagnose(compiler_, function.getParamDecl(index)->getLocation(), "compose content requires a concrete lambda type that preserves its composition protocol");
                    return;
                }
                if (!owned_captures(*closure)) return;
                auto *operation = closure->getLambdaCallOperator();
                if (!marked(*operation, compose_marker))
                    operation->addAttr(clang::AnnotateAttr::Create(compiler_.getASTContext(), compose_marker, nullptr, 0, operation->getSourceRange()));
                process(*operation);
            }
        auto mangler = std::unique_ptr<clang::MangleContext>(compiler_.getASTContext().createMangleContext());
        std::string identity;
        llvm::raw_string_ostream symbol(identity);
        auto declaration = [&]() -> clang::GlobalDecl {
            if (auto *constructor = llvm::dyn_cast<clang::CXXConstructorDecl>(&function)) return {constructor, clang::Ctor_Complete};
            if (auto *destructor = llvm::dyn_cast<clang::CXXDestructorDecl>(&function)) return {destructor, clang::Dtor_Complete};
            return &function;
        }();
        if (mangler->shouldMangleDeclName(&function)) mangler->mangleName(declaration, symbol);
        else symbol << function.getQualifiedNameAsString();
        if (!function.isExternallyVisible()) {
            auto file = compiler_.getSourceManager().getPresumedLoc(function.getLocation());
            auto directory = compiler_.getVirtualFileSystem().getCurrentWorkingDirectory();
            if (file.isValid() && directory)
                symbol << ':' << std::filesystem::path(file.getFilename()).lexically_relative(*directory).generic_string();
        }
        if (compose && !function.getReturnType()->isDependentType() && !function.getReturnType()->isUndeducedType() &&
            !element_result(function.getReturnType())) {
            diagnose(compiler_, function.getLocation(), "compose function must return Element or ComposeResult<Element>");
            return;
        }
        auto *method = llvm::dyn_cast<clang::CXXMethodDecl>(&function);
        bool closure = method && method->getParent()->isLambda();
        Region region{hash(identity), 0, !closure || compose, compose};
        clang::Sema::ContextRAII scope(compiler_.getSema(), &function);
        function.setBody(transform(function.getBody(), region));
    }
    void collect(clang::Decl *declaration) {
        if (auto *function = llvm::dyn_cast<clang::FunctionDecl>(declaration)) process(*function);
        else if (auto *function = llvm::dyn_cast<clang::FunctionTemplateDecl>(declaration)) {
            process(*function->getTemplatedDecl());
            for (auto *specialization : function->specializations()) process(*specialization);
        } else if (auto *context = llvm::dyn_cast<clang::DeclContext>(declaration)) {
            if (declaration->isFromASTFile()) return;
            for (auto *child : context->decls()) collect(child);
        }
    }
  public:
    explicit Consumer(clang::CompilerInstance &compiler) : compiler_(compiler) {}
    bool HandleTopLevelDecl(clang::DeclGroupRef group) override {
        for (auto *declaration : group) collect(declaration);
        return !compiler_.getDiagnostics().hasErrorOccurred();
    }
    void HandleInlineFunctionDefinition(clang::FunctionDecl *function) override {
        if (!function->getParentFunctionOrMethod()) process(*function);
    }
    void HandleCXXImplicitFunctionInstantiation(clang::FunctionDecl *function) override { process(*function); }
    void HandleInterestingDecl(clang::DeclGroupRef group) override {
        for (auto *declaration : group) collect(declaration);
    }
};
}

export auto vui_compose_consumer(clang::CompilerInstance &compiler) -> std::unique_ptr<clang::ASTConsumer> {
    return std::make_unique<vui_compose::Consumer>(compiler);
}
