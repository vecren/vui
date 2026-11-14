#include <clang/AST/Attr.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendPluginRegistry.h>
#include <clang/Sema/ParsedAttr.h>
#include <clang/Sema/Sema.h>

import std;
import vui.compose.plugin;

namespace {
template <bool Content> class Attribute final : public clang::ParsedAttrInfo {
  public:
    Attribute() {
        static constexpr Spelling spellings[]{{clang::ParsedAttr::AS_CXX11, Content ? "vui::content" : "vui::compose"}};
        Spellings = spellings;
    }
    auto handleDeclAttribute(clang::Sema &sema, clang::Decl *declaration, const clang::ParsedAttr &attribute) const -> AttrHandling override {
        auto fail = [&](llvm::StringRef message) {
            auto id = sema.getDiagnostics().getCustomDiagID(clang::DiagnosticsEngine::Error, "%0");
            sema.Diag(attribute.getLoc(), id) << message;
            return AttributeNotApplied;
        };
        if constexpr (Content) {
            if (!llvm::isa<clang::ParmVarDecl>(declaration)) return fail("vui::content requires a function parameter");
        } else {
            if (auto *function = llvm::dyn_cast<clang::FunctionTemplateDecl>(declaration)) declaration = function->getTemplatedDecl();
            auto *function = llvm::dyn_cast<clang::FunctionDecl>(declaration);
            if (!function) return fail("vui::compose requires a function or lambda call operator");
            if (auto *method = llvm::dyn_cast<clang::CXXMethodDecl>(function); method && !method->isStatic() && !method->getParent()->isLambda())
                return fail("vui::compose requires a free function, static method or lambda");
            if (function->isConstexpr() || function->isConsteval() || function->isVariadic())
                return fail("vui::compose does not support constexpr, consteval or variadic functions");
            for (auto *parameter : function->parameters()) {
                auto type = parameter->getType();
                if (type->isRValueReferenceType() || (type->isLValueReferenceType() && !type.getNonReferenceType().isConstQualified()))
                    return fail("vui::compose parameters must be replayable values or const references");
            }
        }
        declaration->addAttr(clang::AnnotateAttr::Create(sema.Context, Content ? "vui.compose.content.v2" : "vui.compose.v2", nullptr, 0, attribute.getRange()));
        return AttributeApplied;
    }
};
class Action final : public clang::PluginASTAction {
    auto CreateASTConsumer(clang::CompilerInstance &compiler, llvm::StringRef) -> std::unique_ptr<clang::ASTConsumer> override {
        return vui_compose_consumer(compiler);
    }
    bool ParseArgs(const clang::CompilerInstance &, const std::vector<std::string> &arguments) override { return arguments.empty(); }
    auto getActionType() -> ActionType override { return AddBeforeMainAction; }
};
clang::ParsedAttrInfoRegistry::Add<Attribute<false>> compose("vui.compose", "vui composition function");
clang::ParsedAttrInfoRegistry::Add<Attribute<true>> content("vui.content", "vui composition content parameter");
clang::FrontendPluginRegistry::Add<Action> action("vui-compose-plugin", "vui composition calls and state slots");
}
