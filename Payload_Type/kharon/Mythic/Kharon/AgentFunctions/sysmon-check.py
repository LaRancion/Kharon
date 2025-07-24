from mythic_container.MythicCommandBase import *
from mythic_container.MythicRPC import *
import logging

from .Utils.u import *

class SysmoncheckArguments( TaskArguments ):
    def __init__(self, command_line, **kwargs):
        super().__init__(command_line, **kwargs)
        self.args = []

    async def parse_arguments(self):
        pass

class SysmoncheckCommand( CommandBase ):
    cmd         = "sysmon-check"
    needs_admin = False
    help_cmd    = "sysmon-check"
    description = \
    """
    Verify if Sysmon is running by checking the registry and listing Minifilter drivers.

    Behavior: Beacon Object File
    """
    version = 1
    author = "@Oblivion"
    argument_class = SysmoncheckArguments
    browser_script = BrowserScript(script_name="usf_new", author="@Oblivion", for_new_ui=True)
    attributes = CommandAttributes(
        supported_os=[SupportedOS.Windows],
    )

    async def create_go_tasking(self, task: PTTaskMessageAllData) -> PTTaskCreateTaskingMessageResponse:
    
        content:bytes = await get_content_by_name( "kh_enumsysmon.x64.o", task.Task.ID )

        task.args.add_arg("bof_file", content.hex())

        return PTTaskCreateTaskingMessageResponse(
            TaskID=task.Task.ID,
            CommandName="exec-bof",
            TokenID=task.Task.TokenID
        )
  
    async def process_response(self, task: PTTaskMessageAllData, response: any) -> PTTaskProcessResponseMessageResponse:
        resp = PTTaskProcessResponseMessageResponse(TaskID=task.Task.ID, Success=True)
        return resp


