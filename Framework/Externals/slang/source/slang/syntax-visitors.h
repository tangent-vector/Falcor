#ifndef RASTER_RENDERER_SYNTAX_PRINTER_H
#define RASTER_RENDERER_SYNTAX_PRINTER_H

#include "diagnostics.h"
#include "syntax.h"

namespace Slang
{
    class CompileRequest;
    class ShaderCompiler;
    class ShaderLinkInfo;
    class ShaderSymbol;

    class TranslationUnitRequest;

    void checkTranslationUnit(
        TranslationUnitRequest* translationUnit);

    // Look for a module that matches the given name:
    // either one we've loaded already, or one we
    // can find vai the search paths available to us.
    //
    // Needed by import declaration checking.
    //
    // TODO: need a better location to declare this.
    RefPtr<ProgramSyntaxNode> findOrImportModule(
        CompileRequest*     request,
        String const&       name,
        CodePosition const& loc);
}

#endif