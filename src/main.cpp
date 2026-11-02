#include <cstdlib>
#include <iostream>
#include <string>
#include <fstream>
#include <iterator>
#include <filesystem>
#include <string>
#include <map>
#include <array>
#include <cstdio>
#include <sstream>
#include <vector>

#include <cpr/cpr.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
// adding a string replace
/**
 * we ar usning arghument replacemnt so we dont have to write
 * s,me skill again again
 * cpp doesnt provide string replacement
 * have to build own
 */

struct SkillInfo
{
    std::string name;
    std::string description;
    bool is_forked = false;
};

std::map<std::string, SkillInfo> available_skills;

void replace_all(std::string &str, const std::string &from, const std::string &to)
{
    if (from.empty())
        return;
    size_t start_pos = 0;
    while ((start_pos = str.find(from, start_pos)) != std::string::npos)
    {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length();
    }
}

std::string read_skill_body(const std::filesystem::path &skill_path)
{
    std::string body = "";
    if (std::filesystem::exists(skill_path))
    {
        std::ifstream file(skill_path); // opeing the file
        std::string line;

        int dash_count = 0;

        while (std::getline(file, line))
        {
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }

            if (line == "---")
            {
                dash_count++;
                continue;
            }

            if (dash_count >= 2)
            {
                body += line + '\n';
            }
        }
    }
    return body;
}

std::string run_agent_loop(
    json &messages,
    const json &tools,
    const std::string &base_url,
    const std::string &api_key,
    const std::map<std::string, SkillInfo> &available_skills

)
{
    while (true)
    {
        json request_body = {
            {"model", "anthropic/claude-haiku-4.5"}, // Required model name
            {"messages", messages},
            {"tools", tools}};

        std::cerr << "DEBUG 4: Sending request to LLM\n";
        cpr::Response response = cpr::Post(
            cpr::Url{base_url + "/chat/completions"},
            cpr::Header{
                {"Authorization", "Bearer " + api_key},
                {"Content-Type", "application/json"}},
            cpr::Body{request_body.dump()});

        if (response.status_code != 200) // if HTTP request doent succeeded
        {
            std::cerr << "HTTP error: " << response.status_code << "\n"
                      << response.text << std::endl;
            return "";
        }
        json result = json::parse(response.text); // the server return the respnse and response.text conatain its body as stirng

        if (!result.contains("choices") || result["choices"].empty()) // if result doesnt contian choices or if choices is their but empty
        {
            std::cerr << "No choices in response" << std::endl;
            return "";
        }
        auto message = result["choices"][0]["message"]; // first choice
        std::cerr << "\nDEBUG: HTTP status = " << response.status_code << "\n";
        std::cerr << "DEBUG: LLM message = " << message.dump(2) << "\n";

        messages.push_back(message);

        if (message.contains("tool_calls") && !message["tool_calls"].empty())
        {
            for (auto &tool_call : message["tool_calls"]) // tool call extracted by the llm first tool
            {                                             //}
                std::string call_id = tool_call["id"];
                std::string function_name = tool_call["function"]["name"];
                std::string tool_output = "";

                std::string argument_str = tool_call["function"]["arguments"];
                json args = json::parse(argument_str);

                if (function_name == "Read")
                {
                    // parsing the arguments JSON string
                    // std::string argument_str = tool_call["function"]["arguments"]; // value of itself is JSON formatted string --"{\"file_path\":\"main.cpp\"}"
                    // json args = json::parse(argument_str);                         // convert the json string to a string object -->{"file_path":"main.cpp"}
                    std::string file_path = args["file_path"]; // using the file path as the key

                    // reading the file from the local file system

                    /**
                     * the llm chooces the pathe
                     *
                     * cpp program receives the path
                     * cpp program performs the file operation
                     */
                    if (std::filesystem::is_directory(file_path))
                    {
                        tool_output = "Error: " + file_path + " is a directory, not a file";
                    }
                    else
                    {
                        std::ifstream file(file_path);
                        if (!file.is_open())
                        {
                            tool_output = "Error: Failed to open the file" + file_path;
                        }
                        else
                        {
                            tool_output = std::string(
                                (std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());
                        }
                    }
                }
                else if (function_name == "Write")
                {
                    std::string file_path = args["file_path"];
                    std::string content = args["content"];

                    // ensuring parent directories exist if a nested path provided
                    std::filesystem::path p(file_path);
                    if (p.has_parent_path())
                    {
                        std::filesystem::create_directories(p.parent_path());
                    }
                    // ofstream creates the file if missing or overwrites it by default
                    std::ofstream out_file(file_path, std::ios::out | std::ios::trunc);
                    if (!out_file.is_open())
                    {
                        tool_output = "Error:failed to write to file" + file_path;
                    }
                    else
                    {
                        out_file << content;
                        out_file.close();
                        tool_output = "Successfully wrote to" + file_path;
                    }
                }
                else if (function_name == "Bash")
                {
                    std::string command = args["command"];        // args contain json object
                    std::string full_command = command + " 2>&1"; // ls -la 2>&1
                    /*popen expect command as C style string +snted by const char*
                        full_command.c_str() provides a pointer to the string
                        null-terminated character daTA

                        "r"--> means open the pipe for reading
                        "w"--> ur program, writes to the command stdin

                        popen starts a command interpreter to execute
                        the command and establishes

                         the pipe between command and ur program

                        popen execute the command be3acuse may originate
                        from ai model must not blindly trust it
                        arbitaty command may expose secrets peofrm unwanted action
                    */
                    FILE *pipe = popen(full_command.c_str(), "r");
                    if (!pipe) // if pipe is nullptr
                    {
                        tool_output = "Error: Failed to run the command";
                        /**
                         * However, this message is generic: a failure to
                         *  create the pipe does not
                         * necessarily mean that the shell command itself failed.
                         */
                    }
                    else
                    {
                        /**
                         * buffer is a temporary area of memory used to hold data
                         * while processing it
                         * 256 is the size of each read operation
                         *
                         */
                        std::array<char, 256> buffer;                                // crwating a buffer array
                        while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) // collecting commands output
                        {
                            /**
                             * buffer.data --> return pointer to the first char of thearray
                             * fgets(buffer.data(), buffer.size(), pipe)
                             * ask C to read character from pipe into buffere
                             */
                            tool_output += buffer.data();
                        }
                        pclose(pipe); // affter collecting the output program must close the pipe
                    }
                }

                else if (function_name == "Skill" || function_name == "skill")
                {

                    // In case the model calls a tool named 'Skill' with {"skill": "<name>"}
                    std::string target_name = "";

                    if (args.contains("skill_name"))
                    {
                        target_name = args["skill_name"];
                    }
                    if (args.contains("skill"))
                    {
                        target_name = args["skill"];
                    }
                    else if (args.contains("name"))
                    {
                        target_name = args["name"];
                    }

                    auto it = available_skills.find(target_name);
                    // why
                    if (target_name.empty() && available_skills.size() == 1)
                    {
                        target_name = available_skills.begin()->first;
                    }
                    if (!target_name.empty() && available_skills.find(target_name) != available_skills.end())
                    {
                        std::filesystem::path skill_dir = std::filesystem::path(".claude/skills") / target_name;
                        std::filesystem::path skill_path = skill_dir / "SKILL.md";
                        std::string body = read_skill_body(skill_path);

                        std::string skill_args = "";
                        if (args.contains("args") && !args["args"].is_null())
                        {
                            skill_args = args["args"].get<std::string>();
                        }
                        // learntgsi part
                        if (!skill_args.empty())
                        {
                            std::vector<std::string> args_list;
                            std::istringstream arg_stream(skill_args);
                            std::string arg;
                            while (arg_stream >> arg)
                            {
                                args_list.push_back(arg);
                            }

                            replace_all(body, "$ARGUMENTS", skill_args);
                            for (size_t i = 0; i < args_list.size(); ++i)
                            {
                                replace_all(body, "$ARGUMENTS[" + std::to_string(i) + "]", args_list[i]);
                                replace_all(body, "$" + std::to_string(i), args_list[i]);
                            }
                        }

                        if (it->second.is_forked)
                        {

                            json sub_message = json::array();
                            sub_message.push_back(json::object({{"role", "user"},
                                                                {"content", body}}));

                            std::string sub_result = run_agent_loop(
                                sub_message,
                                tools,
                                base_url, api_key,
                                available_skills);

                            tool_output = "Skill " + target_name + "ran in a seperate context and return: " + sub_result;
                        }
                        else
                        {
                            std::string header = "Skill" + target_name + "(located at " + skill_dir.string() + ")\n" + "Paths in the instructions below ar relative to that folder.\n\n";
                            tool_output = header + body;
                        }
                    }
                    else
                    {
                        tool_output = "Error:Skill not found";
                    }
                } // last
                else if (available_skills.find(function_name) != available_skills.end())
                {
                    auto it = available_skills.find(function_name);
                    std::filesystem::path skill_dir = std::filesystem::path(".claude/skills") / function_name;
                    std::filesystem::path skill_path = skill_dir / "SKILL.md";
                    std::string body = read_skill_body(skill_path);

                    if (it->second.is_forked)
                    {
                        json sub_message = json::array();
                        sub_message.push_back(json::object({{"role", "user"},
                                                            {"content", body}}));

                        std::string sub_result = run_agent_loop(
                            sub_message,
                            tools,
                            base_url, api_key,
                            available_skills);

                        tool_output = "Skill " + function_name + "ran in a seperate context and return: " + sub_result;
                    }
                    else
                    {
                        std::string header = "Skill" + function_name + "(located at" + skill_dir.string() + ")\n" + "Paths in the instructions below ar relative to that folder.\n\n";
                        tool_output = header + body;
                    }
                }

                messages.push_back({{"role", "tool"},
                                    {"tool_call_id", call_id},
                                    {"content", tool_output}});
            }
        }
        else
        {
            // no tool call
            if (message.contains("content") && !message["content"].is_null())
            {
                // Fallback to standard mesage content if no tool call was made
                return message["content"].get<std::string>();
            }
            return "";
        }
    }
}
void trim_str(std::string &s)
{
    size_t p = s.find_first_not_of(" \t\r\n");
    size_t q = s.find_last_not_of(" \t\r\n");
    if (p == std::string::npos)
        s.clear();
    else
        s = s.substr(p, q - p + 1);
}

// c++n expect main
// to receive cpmmand line argumenrt as an array of C style string
int main(int argc, char *argv[]) // argc --> argument count  how many command line  argument werte passed. |||| argv( arravy of strings) is the command line passed to c++ program
{
    //./your_program.sh ,"-p ,"/apple /grape 4127"
    if (argc < 3 || std::string(argv[1]) != "-p")
    {
        std::cerr << "Expected first argument to be '-p'" << std::endl;
        return 1;
    }

    std::string prompt = argv[2]; // getting the prompt
    std::cerr << "DEBUG 1: Program started. Prompt = " << prompt << "\n";

    if (prompt.empty())
    {
        std::cerr << "Prompt must not be empty";
    }

    // export OPENROUTER_API_KEY =sk - or -v1 - 174ed2c94730898726c15784b4b1773fba9cf15bab29d258df64114d03a75bfa // api key is for authorisation
    //  export OPENROUTER_BASE_URL = "https://openrouter.ai/api/v1"        // where to send the message /communicate with LLM

    const char *base_url_env = std::getenv("OPENROUTER_BASE_URL"); // gets url from my system
    const char *api_key_env = std::getenv("OPENROUTER_API_KEY");

    std::string api_key = api_key_env ? api_key_env : "";
    std::string base_url = base_url_env ? base_url_env : "https://openrouter.ai/api/v1";

    if (api_key.empty())
    {
        std::cerr << "OPENROUTER_API_KEY is not set" << std::endl;
        return 1;
    }
    std::cerr << "DEBUG 2: API key found\n";

    // =========================================================================
    // 1. DISCOVER AVAILABLE SKILLS FIRST (Populate available_skills)
    // =========================================================================

    std::map<std::string, SkillInfo> available_skills;
    // key--> skill name , value=skill description

    // path to the skills diectory relative to where program is executed
    std::filesystem::path skills_dir = ".claude/skills"; //  stores the path to skill directory/path inside .claude directory

    // stores path no the actual folder

    // chekcing if .claude/skills is actually exists and is a folder before scanning
    if (std::filesystem::exists(skills_dir) && std::filesystem::is_directory(skills_dir))
    {
        // Iterating thorugh each elemnt inside ".claude/skills/" "./claude/skills/apple"
        for (const auto &entry : std::filesystem::directory_iterator(skills_dir))
        {
            // Each skill is a folder , so loose file if any exisy
            if (entry.is_directory())
            {
                // construct the path to SKILL.md inside subfolder ".claude/skills/<folder>/SKILL.md"
                std::filesystem::path skill_file = entry.path() / "SKILL.md"; // '/' joins filesystem paths
                // proceed if SKILL.md actually exists inside this skill folde

                if (std::filesystem::exists(skill_file))
                {
                    std::ifstream file(skill_file); // open the file
                    std::string line;
                    // State flag--> tracks whether ur inside "---" formatter block
                    /**
                     * frontmatter is a meta data sectio n qat the beginnig of a document often writtemn using
                     * YAML and surrounded by ---
                     */
                    bool in_frontmatter = false;
                    std::string name = "";
                    std::string description = "";
                    bool is_forked = false;
                    while (std::getline(file, line)) // read every line from the file
                    {
                        // while going through every line line may contain ---\r carriage return \n is removed automatically have to remove it
                        if (!line.empty() && line.back() == '\r')
                        {
                            line.pop_back();
                        }

                        // check for the YAML frontmatter boundary marker "---"

                        if (line == "---")
                        {
                            if (!in_frontmatter) // 1st "---" frontmatter
                            {
                                in_frontmatter = true;
                                continue;
                            }
                            else
                            {
                                // second "---" encountered :frontmatter is finished
                                // break out of the loop
                                break;
                            }
                        }
                        // if we are inside the two ---
                        // check this code for later

                        if (in_frontmatter)
                        {

                            if (line.rfind("name: ", 0) == 0)
                            {
                                // extract everythingg after name
                                name = line.substr(5);
                                trim_str(name);
                            }
                            else if (line.rfind("description: ", 0) == 0)
                            {
                                // extract everythinh after descriptiom
                                description = line.substr(12);
                                trim_str(description);
                            }
                            else if (line.rfind("context: ", 0) == 0)
                            {
                                std::string ctx = line.substr(8);
                                trim_str(ctx);
                                // trimming white spacer
                                if (ctx == "fork")
                                {
                                    is_forked = true;
                                }
                            }
                        }
                    }
                    if (!name.empty() && !description.empty())
                    {
                        available_skills[name] = SkillInfo{name, description, is_forked};
                    }
                    if (name.empty())
                    {
                        name = entry.path().filename().string();
                    }
                }
            }
        }
    }

    // =========================================================================
    // STAGE SKILL STACKING AND INVOCATION
    // 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
    // skill is reusable instructon template wher as argument is u passass template i.e giving additional knowledge so it can perform task
    // == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == =

    std::vector<std::string> invoked_skills;
    std::string remaining_args = "";

    std::istringstream iss(prompt); // convert  stirng to stream so we can read it word by word
    std::string token;
    size_t consumed_chars = 0;

    while (iss >> token) // putitng word in token /apple
    {
        bool is_valid_skill = false;
        size_t token_pos = prompt.find(token, consumed_chars);
        if (token.size() > 1 && token[0] == '/')
        {
            std::string candidate_name = token.substr(1);                                                             // apple
            std::filesystem::path skill_path = std::filesystem::path(".claude/skills") / candidate_name / "SKILL.md"; // building path

            if (std::filesystem::exists(skill_path))
            {
                invoked_skills.push_back(candidate_name); // adding it to the vector
                is_valid_skill = true;
                size_t token_pos = prompt.find(token, consumed_chars);
                consumed_chars = token_pos + token.size();
                // we need to fnmd the position wher e skill stops and  argument  starts point sto index where it start
            }
        }

        if (!is_valid_skill)
        {

            if (token_pos != std::string::npos)
            {
                remaining_args = prompt.substr(token_pos);
            }
            break;
        }
    }
    std::vector<std::string> substituted_skill_messages; // storing each skill with body in the vector with header

    if (!invoked_skills.empty())
    {
        std::vector<std::string> args_list;
        std::istringstream arg_stream(remaining_args);
        std::string arg;

        while (arg_stream >> arg)
        {
            args_list.push_back(arg); // storing each argument in vector
        }
        // std::string combined_prompt = "";

        for (const auto &skill_name : invoked_skills)
        {
            std::filesystem::path skill_dir = std::filesystem::path(".claude/skills") / skill_name;
            std::filesystem::path skill_path = skill_dir / "SKILL.md";
            std::string body = read_skill_body(skill_path);

            if (!body.empty())
            {
                for (size_t i = 0; i < args_list.size(); i++)
                {
                    replace_all(body, "$ARGUMENTS[" + std::to_string(i) + "]", args_list[i]); // replacing all argument blanks with actual argument
                    replace_all(body, "$" + std::to_string(i), args_list[i]);
                }

                replace_all(body, "$ARGUMENTS", remaining_args);
                // combined_prompt += body;// muliplt skill can be invoke together

                std::string header = "Skill: " + skill_name + "(located at " + skill_dir.string() + ")\n" + "Path in the instruction are relative to that folder.\n\n";

                substituted_skill_messages.push_back(header + body);
            }
        }
    }
    std::cerr << "DEBUG 3: Expanded prompt = [" << prompt << "]\n";

    /*

    -------------------- ONE SKILLED CODE-------------------------------------
    if (prompt.length() > 1 && prompt[0] == '/') // /apple
    {
        size_t first_space = prompt.find(' ');
        // sepratre the sklill name form its argument
        std::string requested_skill;   // store skill na,me
        std::string argument_str = ""; // store everything after skill name

        if (first_space != std::string::npos)
        {
            requested_skill = prompt.substr(1, first_space - 1);//apple
            argument_str = prompt.substr(first_space + 1);// argument
        }
        else
        {
            requested_skill = prompt.substr(1);
        }

        // triming the trailing white space fro the skill name

        size_t endpos = requested_skill.find_last_not_of(" \t\r\n");

        if (endpos != std::string::npos)
        {
            requested_skill.erase(endpos + 1); // "appl
        } //  "-> appl
        else
        {
            requested_skill.clear();//makes the string empty
        }
        std::stringstream ss(argument_str); // take word seprated by space
        std::string arg;

        while (ss >> arg)
        {
            args_list.push_back(arg);
        } // converting string argument into a vector

        // locate the skill file

        std::filesystem::path target_skill =
            std::filesystem::path(".claude/skills") / requested_skill / "SKILL.md";
        // '/' joind the path component
        .claude/skills is where all skills are stored
        //.claude/skills/apple/SKILL.md// could have usee the + operator too


        // if the targeted fiel skill

        if (!skill_body.empty())
        {
            // substitue all placeholder in the body befor using it as the prompt
            // replace_all(skill_body,"$ARGUMENTS",argument_str);

            for (size_t i = 0; i < args_list.size(); ++i)
            {
                replace_all(skill_body, "$ARGUMENTS[" + std::to_string(i) + "]", args_list[i]);
                replace_all(skill_body, "$" + std::to_string(i), args_list[i]);
            }

            replace_all(skill_body, "$ARGUMENTS", argument_str);
            prompt = skill_body;
        }
    }
}*/
    //__________________________________________________________________________________
    // PROGRESSIVE DISCLOSURE (System Prompt Discovery)
    //__________________________________________________________________________

    //____________________________________________
    // INITIALIZE CONVERSATION HISTORY
    //____________________________________________
    json messages = json::array();
    if (!available_skills.empty())
    {
        std::string system_prompt = "You have access to the following skills:\n\n";
        for (const auto &[skill_name, info] : available_skills)
        {
            system_prompt += "- " + info.name + ": " + info.description + "\n";
        }
        if (!system_prompt.empty() && system_prompt.back() == '\n')
        {
            system_prompt.pop_back();
        }
        // pushing th esystem prompt
        messages.push_back(json::object({{"role", "system"},
                                         {"content", system_prompt}}));
    }
    // push the user prompt SECOND
    if (!substituted_skill_messages.empty())
    {
        for (const auto &msg_body : substituted_skill_messages)
        {
            messages.push_back(json::object({{"role", "user"},
                                             {"content", msg_body}}));
        }
    }
    else
    {
        messages.push_back(json::object({{"role", "user"},
                                         {"content", prompt}}));
    }

    //____________________________________
    // TOOL DEFINATIONS(Read,Write,Bash)
    //____________________________________
    json tools = json::array();

    // Tool 1: Read
    tools.push_back(json::object({{"type", "function"},
                                  {"function", json::object({{"name", "Read"},
                                                             {"description", "Read and return the contents of a file"},
                                                             {"parameters", json::object({{"type", "object"},
                                                                                          {"properties", json::object({{"file_path", json::object({{"type", "string"},
                                                                                                                                                   {"description", "The path to the file to read"}})}})},
                                                                                          {"required", json::array({"file_path"})}})}})}}));

    // Tool 2: Write
    tools.push_back(json::object({{"type", "function"},
                                  {"function", json::object({{"name", "Write"},
                                                             {"description", "Write content to a file"},
                                                             {"parameters", json::object({{"type", "object"},
                                                                                          {"required", json::array({"file_path", "content"})},
                                                                                          {"properties", json::object({{"file_path", json::object({{"type", "string"},
                                                                                                                                                   {"description", "The path of the file to write to"}})},
                                                                                                                       {"content", json::object({{"type", "string"},
                                                                                                                                                 {"description", "The content to write to the file"}})}})}})}})}}));
    tools.push_back(json::object({{"type", "function"},
                                  {"function", json::object({{"name", "Bash"},
                                                             {"description", "Execute a shell command"},
                                                             {"parameters", json::object({{"type", "object"},
                                                                                          {"required", json::array({"command"})},
                                                                                          {"properties", json::object({{"command", json::object({{"type", "string"},
                                                                                                                                                 {"description", "The command to execute"}})}})}})}

                                               })}

    }

                                 ));

    tools.push_back(json::object({{"type", "function"},
                                  {"function", json::object({{"name", "Skill"},
                                                             {"description", "Load a skill's instruction into the conversation"},
                                                             {"parameters", json::object({{"type", "object"},
                                                                                          {"required", json::array({"name"})},
                                                                                          {"properties", json::object({{"name", json::object({{"type", "string"},
                                                                                                                                              {"description", "The name of the skill to use"}})},
                                                                                                                       {"args", json::object({{"type", "string"},
                                                                                                                                              {"description", "Optional arguments for the skill"}})}})}})}})}})

    );
    // =========================================================================
    // 5. RUN THE MAIN AGENT LOOP & PRINT RESULT
    // =========================================================================
    std::string result = run_agent_loop(messages, tools, base_url, api_key, available_skills);
    std::cout << result;

    return 0;
}

/*json request_body = {
    {"model", "anthropic/claude-haiku-4.5"}, // Required model name
    {"messages", json::array({{{"role", "system"},
                               {"content", "You are a precise assistant. Examine the tools provided in this request, count them, and respond with only the number."}},
                              {{"role", "user"},
                               {"content", prompt}}})},
    {"tools", json::array({{{"type", "function"},
                            {"function", {{"name", "Read"}, {"description", "Read and return the contents of a file"}, {"parameters", {{"type", "object"}, {"properties", {{"file_path", {{"type", "string"}, {"description", "The path to the file to read"}}}}}, {"required", json::array({"file_path"})}}}}}}})}};

cpr::Response response = cpr::Post(
    cpr::Url{base_url + "/chat/completions"},
    cpr::Header{
        {"Authorization", "Bearer " + api_key},
        {"Content-Type", "application/json"}},
    cpr::Body{request_body.dump()});
/**
 *
 *   cpr::Response response = cpr::Post(
cpr::Url{base_url + "/chat/completions"},
cpr::Header{
    {"Authorization", "Bearer " + api_key},
    {"Content-Type", "application/json"}},
cpr::Body{request_body.dump()});
send the request to llm


cpr --> librarty that makes http request
POST--> send the http request to api

Read main.cpp and explain what it does.

ur prorgram creates  ajson request with prompt model and avialbe tools
base_url = "https://openrouter.ai/api/v1"+ "/chat/completions"
api endpoint that recieves ur api request
https://openrouter.ai/api/v1/chat/completions

cpr::Header{ provide information about the request
{"Authorization", "Bearer " + api_key},--> send the api key to server so server can authenticate it
{"Content-Type", "application/json"}--> this tell that request body contains JSON
cpr::Body{request_body.dump()});--> .dump convert normal string inb to json object

200--> succed
400-> invalid reuest
401--> authentication failed
403--> acess forbiden
429--> rate limit issues
500--> rever error


          LLM RESPONSE
           |
           v
  Does it contain
  a tool request?
     /         \
   YES          NO
    |            |
    v            v
Check tool       Check content
name             field
    |            |
    v            v
Execute Read    Print text
    |codecrafters testcodecrafters testcodecrafters test
    v
Print file
contents

 */

/* if (response.status_code != 200) // if HTTP request doent succeeded
 {
     std::cerr << "HTTP error: " << response.status_code << "\n"
               << response.text << std::endl;
     return 1;
 }
 // string string containing json
 json result = json::parse(response.text); // the server return the respnse and response.text conatain its body as stirng

 if (!result.contains("choices") || result["choices"].empty()) // if result doesnt contian choices or if choices is their but empty
 {
     std::cerr << "No choices in response" << std::endl;
     return 1;
 }*/
/*  auto message = result["choices"][0]["message"]; // first choice

  if (message.contains("tool_calls") && !message["tool_calls"].empty())
  {
      auto tool_call = message["tool_calls"][0];                 // tool call extracted by the llm first tool
      std::string function_name = tool_call["function"]["name"]; // extract the name of the requested function
      if (function_name == "Read")
      {
          // parsing the arguments JSON string
          std::string argument_str = tool_call["function"]["arguments"]; // value of itself is JSON formatted string --"{\"file_path\":\"main.cpp\"}"
          json args = json::parse(argument_str);                         // convert the json string to a string object -->{"file_path":"main.cpp"}
          std::string file_path = args["file_path"];                     // using the file path as the key

          // reading the file from the local file system

          /**
           * the llm chooces the pathe
           *
           * cpp program receives the path
           * cpp program performs the file operation
           */
/* std::ifstream file(file_path);
   if (!file.is_open())
   {
       std::cerr << "Failed to open the file " << file_path << std::endl;
       return 1;
   }
   std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
   // read all the characters from the file and store them in a string
   // read the entire file

   // print raw file content directly
   std::cout << content;
}
}
// no tool call
/** else if (message.contains("content") && !message["content"].is_null())
{
// Fallback to standard mesage content if no tool call was made
std::cout << message["content"].get<std::string>();
}

return 0;
}*/

/**
 * {
  "choices": [
    {
      "index": 0,
      "message": {
        "role": "assistant",
        "content": null,
        "tool_calls": [
          {
            "id": "call_abc123",
            "type": "function",
            "function": {
              "name": "Read",
              "arguments": "{\"file_path\": \"/path/to/file.txt\"}"
            }
          }
        ]
      },
      "finish_reason": "tool_calls"
    }
  ]
}
 *
 *
 *
 *
 *
 * {
  "tool_calls": [
    {
      "function": {
        "name": "read"
      }
    },
    {
      "function": {
        "name": "write"
      }
    }
  ]
}





 */
