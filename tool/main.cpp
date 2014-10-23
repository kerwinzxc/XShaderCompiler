/*
 * HLSL Offline Translator main file
 * 
 * This file is part of the "HLSL Translator" (Copyright (c) 2014 by Lukas Hermanns)
 * See "LICENSE.txt" for license information.
 */

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <vector>
#include <HT/Translator.h>


using namespace HTLib;

/* --- Classes --- */

class OutputLog : public HTLib::Logger
{
    
    public:
        
        void Info(const std::string& message) override
        {
            infos_.push_back(message);
        }

        void Warning(const std::string& message) override
        {
            warnings_.push_back(message);
        }

        void Error(const std::string& message) override
        {
            errors_.push_back(message);
        }

        void Report()
        {
            for (const auto& msg : infos_)
                std::cout << msg << std::endl;
            infos_.clear();
            
            if (!warnings_.empty())
            {
                PrintHead(std::to_string(warnings_.size()) + " WARNING(S)");

                for (const auto& msg : warnings_)
                    std::cout << msg << std::endl;
                warnings_.clear();
            }

            if (!errors_.empty())
            {
                PrintHead(std::to_string(errors_.size()) + " ERROR(S)");

                for (const auto& msg : errors_)
                    std::cerr << msg << std::endl;
                errors_.clear();
            }
        }

    private:

        void PrintHead(const std::string& head)
        {
            std::cout << head << std::endl;
            std::cout << std::string(head.size(), '-') << std::endl;
        }
        
        std::vector<std::string> infos_;
        std::vector<std::string> warnings_;
        std::vector<std::string> errors_;

};

class IncludeStreamHandler : public IncludeHandler
{
    
    public:
        
        std::shared_ptr<std::istream> Include(const std::string& includeName) override
        {
            return std::make_shared<std::ifstream>(includeName);
        }

};


/* --- Globals --- */

std::string entry;
std::string target;
std::string shaderIn = "HLSL5";
std::string shaderOut = "GLSL110";
std::string output;
Options options;


/* --- Functions --- */

static void ShowHint()
{
    std::cout << "no input : enter \"HLSLOfflineTranslator help\"" << std::endl;
}

static void ShowHelp()
{
    std::cout << "Usage:" << std::endl;
    std::cout << "  HLSLOfflineTranslator (Options [FILE])+" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -entry NAME         HLSL shader entry point" << std::endl;
    std::cout << "  -target TARGET      Shader target; valid values:" << std::endl;
    std::cout << "                        'vertex'" << std::endl;
    std::cout << "                        'fragment'" << std::endl;
    std::cout << "                        'geometry'" << std::endl;
    std::cout << "                        'tesscontrol'" << std::endl;
    std::cout << "                        'tessevaluation'" << std::endl;
    std::cout << "                        'compute'" << std::endl;
    std::cout << "  -shaderin VERSION   HLSL version ('HLSL3', 'HLSL4' or 'HLSL5'); default is HLSL5" << std::endl;
    std::cout << "  -shaderout VERSION  GLSL version ('GLSL110', 'GLSL120', etc.); default is GLSL110" << std::endl;
    std::cout << "  -indent INDENT      Code indentation string; default is 4 blanks" << std::endl;
    std::cout << "  -prefix PREFIX      Prefix for local variables; default '_'" << std::endl;
    std::cout << "  -output FILE        GLSL output file; default is '<InputFile>.glsl'" << std::endl;
    std::cout << "  -noblanks           No blank lines will be generated" << std::endl;
    std::cout << "  --help, help, -h    Prints this help reference" << std::endl;
    std::cout << "  --version, -v       Prints the version information" << std::endl;
}

static void ShowVersion()
{
    std::cout << "HLSL Translator ( Version " << __HT_VERSION__ << " )" << std::endl;
    std::cout << "Copyright (c) 2014 by Lukas Hermanns" << std::endl;
    std::cout << "3-Clause BSD License" << std::endl;
}

static ShaderTargets TargetFromString(const std::string& target)
{
    if (target == "vertex")
        return ShaderTargets::GLSLVertexShader;
    if (target == "fragment")
        return ShaderTargets::GLSLFragmentShader;
    if (target == "geometry")
        return ShaderTargets::GLSLGeometryShader;
    if (target == "tesscontrol")
        return ShaderTargets::GLSLTessControlShader;
    if (target == "tessevaluation")
        return ShaderTargets::GLSLTessEvaluationShader;
    if (target == "compute")
        return ShaderTargets::GLSLComputeShader;

    throw std::string("invalid shader target \"" + target + "\"");
    return ShaderTargets::GLSLVertexShader;
}

static InputShaderVersions InputVersionFromString(const std::string& version)
{
    #define CHECK_IN_VER(n) if (version == #n) return InputShaderVersions::n

    CHECK_IN_VER(HLSL3);
    CHECK_IN_VER(HLSL4);
    CHECK_IN_VER(HLSL5);

    #undef CHECK_IN_VER

    throw std::string("invalid input shader version \"" + version + "\"");
    return InputShaderVersions::HLSL5;
}

static OutputShaderVersions OutputVersionFromString(const std::string& version)
{
    #define CHECK_OUT_VER(n) if (version == #n) return OutputShaderVersions::n

    CHECK_OUT_VER(GLSL110);
    CHECK_OUT_VER(GLSL120);
    CHECK_OUT_VER(GLSL130);
    CHECK_OUT_VER(GLSL140);
    CHECK_OUT_VER(GLSL150);
    CHECK_OUT_VER(GLSL330);
    CHECK_OUT_VER(GLSL400);
    CHECK_OUT_VER(GLSL410);
    CHECK_OUT_VER(GLSL420);
    CHECK_OUT_VER(GLSL430);
    CHECK_OUT_VER(GLSL440);
    CHECK_OUT_VER(GLSL450);

    #undef CHECK_OUT_VER

    throw std::string("invalid output shader version \"" + version + "\"");
    return OutputShaderVersions::GLSL110;
}

static std::string NextArg(int& i, int argc, char** argv, const std::string& flag)
{
    if (i + 1 >= argc)
        throw std::string("missing next argument after flag \"" + flag + "\"");
    return argv[++i];
}

static void Translate(const std::string& filename)
{
    if (entry.empty())
    {
        std::cerr << "missing entry point" << std::endl;
        return;
    }
    if (target.empty())
    {
        std::cerr << "missing shader target" << std::endl;
        return;
    }

    if (output.empty())
        output = filename + ".glsl";

    /* Translate HLSL file into GLSL */
    std::cout << "translate from " << filename << " to " << output << std::endl;

    auto inputStream = std::make_shared<std::ifstream>(filename);
    std::ofstream outputStream(output);

    OutputLog log;
    IncludeStreamHandler includeHandler;

    try
    {
        auto result = TranslateHLSLtoGLSL(
            inputStream,
            outputStream,
            entry,
            TargetFromString(target),
            InputVersionFromString(shaderIn),
            OutputVersionFromString(shaderOut),
            &includeHandler,
            options,
            &log
        );

        log.Report();

        if (result)
            std::cout << "translation successful" << std::endl;
    }
    catch (const std::string& err)
    {
        std::cerr << err << std::endl;
    }
}


/* --- Main function --- */

int main(int argc, char** argv)
{
    int translationCounter = 0;
    bool showHelp = false;
    bool showVersion = false;

    /* Parse program arguments */
    for (int i = 1; i < argc; ++i)
    {
        try
        {
            auto arg = std::string(argv[i]);

            if (arg == "help" || arg == "--help" || arg == "-h")
                showHelp = true;
            else if (arg == "--version" || arg == "-v")
                showVersion = true;
            else if (arg == "-noblanks")
                options.noblanks = true;
            else if (arg == "-entry")
                entry = NextArg(i, argc, argv, arg);
            else if (arg == "-target")
                target = NextArg(i, argc, argv, arg);
            else if (arg == "-shaderin")
                shaderIn = NextArg(i, argc, argv, arg);
            else if (arg == "-shaderout")
                shaderOut = NextArg(i, argc, argv, arg);
            else if (arg == "-indent")
                options.indent = NextArg(i, argc, argv, arg);
            else if (arg == "-prefix")
                options.prefix = NextArg(i, argc, argv, arg);
            else if (arg == "-output")
                output = NextArg(i, argc, argv, arg);
            else
            {
                Translate(arg);
                ++translationCounter;
            }
        }
        catch (const std::string& err)
        {
            std::cerr << err << std::endl;
            return 0;
        }
    }

    /* Evaluate arguemnts */
    if (showHelp)
        ShowHelp();
    if (showVersion)
        ShowVersion();

    if (translationCounter == 0 && !showHelp && !showVersion)
        ShowHint();

    return 0;
}