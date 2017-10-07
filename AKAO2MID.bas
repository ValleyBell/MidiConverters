Attribute VB_Name = "Module1"
Option Explicit

Private Declare Sub CopyMemory Lib "kernel32" Alias "RtlMoveMemory" _
    (ByRef hpvDest As Any, ByRef hpvSource As Any, ByVal cbCopy As Long)

Private Type AKAO_HEADER
    Signature As String * &H4
    FrameID As Integer
    FrameLength As Integer
    VersionMajor As Byte
    VersionMinor As Byte
    Unknown1 As Long
    Unknown2 As Integer
End Type
Private Type AKAO_DATA_INFO
    Unknown1 As Long
    Unknown2 As Long    ' seems to be FrameID + 62h
    ChnMask As Long
    InsBlkAddr As Long
    UnknwAddr As Long
    ChnOffset(&H0 To &H1F) As Long
End Type
Private Type AKAO_INS_DATA
    BlkID As Byte       ' Instrument Data Block ID (or Sample ID??)
    NoteLow As Byte     ' Lowest Note to play the current sample
    NoteHigh As Byte    ' Highetst Note to play the current sample
    Unknwon3 As Byte    ' Unknown, but seems be equal for all Data Blocks in one Set
    Unknwon4 As Byte    ' Unknown
    Unknwon5 As Byte    ' Unknown, usually a low number < 10h
    Unknwon6 As Byte    ' Unknown, usually a low number < 10h
    Volume As Byte      ' Volume (I guess, it's value is usually >= 70h)
End Type
Private Type AKAO_INS_SET
    DataCount As Integer
    Data() As AKAO_INS_DATA
End Type
Private Type AKAO_INS_BLOCK
    InsCount As Integer
    InsAddr() As Long
    InsSet() As AKAO_INS_SET
End Type

' FourCharCode Constants
Private Const FCC_AKAO As Long = &H4F414B41

Private BIT_SHIFT(&H0 To &H1F) As Long

'Private FileNameType As Byte
'Private FixMidi As Boolean
Private AkaoLen As Long
Private AkaoFile() As Byte
Private MidiLen As Long
Private MidiFile() As Byte
Private MidiPos As Long
Private MidiDelay As Long
Private SurpressDbgCtrls As Boolean

Sub Main()

    Dim FilePath As String
    Dim FileFilter As String
    Dim FileName As String
    Dim FileCount As Long
    'Dim FileType As Byte
    Dim FileList(&H0 To &H7FF) As String
    Dim TempLng As Long
    
    For TempLng = &H0 To &H1E
        BIT_SHIFT(TempLng) = &H2 ^ TempLng
    Next TempLng
    BIT_SHIFT(&H1F) = -&H2 ^ TempLng
    
    ' Works with uncompressed PSFs too
    'FileList(&H0) = "FF7\101 The Prelude.ako"
    'FileList(&H1) = "FF7\110 Fighting.ako"
    'FileList(&H2) = "FF7\FAN2.SND.AKO"
    'FileList(&H3) = "FF7\122 Crazy Motorcycle.ako"
    'FileList(&H4) = "FF7\207 Electric de Chocobo.ako"
    'FileList(&H5) = "FF7\ASERI2.SND.AKO"
    'FileList(&H10) = "104 Don't Be Afraid.minipsf"
    'FileList(&H11) = "105 The Winner.minipsf"
    'FileList(&H12) = "110 Force Your Way.minipsf"
    'FileList(&H13) = "112 Never Look Back.minipsf"
    'FileList(&H14) = "119 The Man With the Machine Gun.minipsf"
    'FileList(&H15) = "308 Odeka ke Chocobo.minipsf"
    'FileList(&H16) = "401 Mods de Chocobo.minipsf"
    'FileList(&H20) = "205 Battle Scene 1.psf"
    'FileList(&H21) = "bFF2_223.bin"
    'FilePath = "D:\VStudio-Programme\VBasic\PSFCnv\FF9\"
    FileName = Command$()
    If FileName = "" Then
        MsgBox "Usage:" & vbNewLine & _
            "akao2mid.exe ""in.ako""" & vbNewLine & _
            "akao2mid.exe ""Path\*.ako""" & vbNewLine & _
            "Use at your own risk. -Valley Bell"
        Exit Sub
    End If
    If Left$(FileName, 1) = """" Then
        FileName = Mid$(FileName, 2)
    End If
    If Right$(FileName, 1) = """" Then
        FileName = Left$(FileName, Len(FileName) - 1)
    End If
    
    TempLng = InStrRev(FileName, "\")
    FilePath = Left$(FileName, TempLng)
    FileFilter = Mid$(FileName, TempLng + 1)
    
    'FixMidi = True
    SurpressDbgCtrls = True 'False
    
    'TempLng = &H4
    
    'Exit Sub
    FileName = Dir(FilePath & FileFilter)
    FileCount = &H0
    Do Until FileName = ""
        FileList(FileCount) = FileName
        FileCount = FileCount + &H1
        
        FileName = Dir()
    Loop
    
    Dim ConvCount As Long
    ConvCount = 0
    For TempLng = &H0 To FileCount - 1
        FileName = FilePath & FileList(TempLng)
        ConvCount = ConvCount + ConvertAkao(FileName)
    Next TempLng
    
    MsgBox "Input files: " & Format$(FileCount) & vbNewLine & _
            "Successful: " & Format$(ConvCount)

End Sub

Private Function ConvertAkao(ByVal FileName As String) As Long

    Dim FilePath As String
    Dim MidAllCnt As Long
    Dim CurPos As Long
    Dim TempLng As Long
    Dim TempFile As String
    'Dim InsData As INSTRUMENT_DATA
    
    TempLng = InStrRev(FileName, "\")
    FilePath = Left$(FileName, TempLng)
    FileName = Mid$(FileName, TempLng + 1)
    
    If Dir(FilePath & FileName, vbHidden) = "" Then
        Debug.Print "File not found!"
        ConvertAkao = 0
        Exit Function
    End If
    
    Debug.Print "Loading " & FileName & "...";
    Open FilePath & FileName For Binary Access Read As #1
        AkaoLen = LOF(1)
        ReDim AkaoFile(&H0 To AkaoLen - 1)
        Get #1, 1, AkaoFile()
    Close #1
    Debug.Print "  OK"
    
    FileName = Left$(FileName, InStrRev(FileName, ".") - 1)
    
    Debug.Print "Converting ..." & vbTab;
    MidAllCnt = 0
    CurPos = &H0
    Do
        Do While CurPos < AkaoLen
            If AkaoFile(CurPos) = &H41 Then
                Call CopyMemory(TempLng, AkaoFile(CurPos), &H4)
                
                If TempLng = FCC_AKAO Then
                    Call CopyMemory(TempLng, AkaoFile(CurPos + &H4), &H4)
                    'If TempLng = &H1000000 And AkaoFile(CurPos + &H8) = &H1 Then
                        Exit Do
                    'Else
                    '    CurPos = CurPos + &H4
                    'End If
                Else
                    CurPos = CurPos + &H1
                End If
            Else
                CurPos = CurPos + &H1
            End If
        Loop
        If CurPos >= AkaoLen Then
            Exit Do
        End If
        
        'TempFile = "_" & Format$(MidAllCnt, "00")
        'Debug.Print "File " & Format$(MidAllCnt + 1) & ": " & FileName & TempFile
        TempFile = FileName & TempFile & ".mid"
        
        Call Akao2MidConversion(CurPos)
        CurPos = &H0
        
        Open FilePath & TempFile For Output As #2
        Close #2
        Open FilePath & TempFile For Binary Access Write As #2
            Put #2, 1, MidiFile()
        Close #2
        
        MidAllCnt = MidAllCnt + 1
        Exit Do
    Loop
    
    Debug.Print Format$(MidAllCnt) & " File" & IIf(MidAllCnt = 1, "", "s") & _
                " saved."
    ConvertAkao = IIf(MidAllCnt >= 1, 1, 0)

End Function

Private Function Akao2MidConversion(ByRef PosStart As Long) As Long

    Dim AkaoHead As AKAO_HEADER
    Dim AkaoLen As Long
    Dim AkaoDInf As AKAO_DATA_INFO
    Dim AkaoIns As AKAO_INS_BLOCK
    Dim ChnCount As Byte
    Dim ChnPos() As Long
    Dim WholeDelay As Long
    Dim FileVer As Byte
    
    Dim TrkPos As Long
    Dim CurPos As Long
    Dim CurChn As Byte
    Dim CmdVal As Byte
    Dim DelayPos As Long
    Dim TempLng As Long
    Dim TempInt As Integer
    Dim TempByt As Byte
    Dim TempSng As Single
    Dim TempArr() As Byte
    Dim TrkEnd As Boolean
    Dim CurOctave As Byte
    Dim LastNote As Byte
    Dim ChnVol As Byte
    Dim LoopIdx As Byte
    Dim CurLoop(&H0 To &HFF) As Byte
    Dim LoopPos(&H0 To &HFF) As Long
    Dim MidCmd As Byte
    Dim NoteVal As Byte
    Dim NextDelay As Byte
    Dim NextNote As Integer
    Dim DelayOverride As Long
    Dim CommonCmd As Boolean
    Dim MstJmpCnt As Long
    Dim MidiChn As Byte
    Dim DrumMode As Boolean
    Dim OctBakM As Byte ' Octave Backup for Melody Mode
    Dim OctBakD As Byte ' Octave Backup for Drum Mode
    
    Dim VolFadeDelay As Long
    Dim VolFadeStart As Long
    Dim VolFadeCur As Long
    Dim VolFadeTo As Long
    Dim LastNoteVol As Byte
    Dim NoteVol As Byte
    
    Dim HadDlyOvr As Boolean
    
    MidiLen = &H40000
    ReDim MidiFile(&H0 To MidiLen - 1)
    
    CurPos = PosStart
    
    With AkaoHead
        Call CopyMemory(ByVal .Signature, AkaoFile(CurPos + &H0), &H4)
        Call CopyMemory(.FrameID, AkaoFile(CurPos + &H4), &H2)
        Call CopyMemory(.FrameLength, AkaoFile(CurPos + &H6), &H2)
        .VersionMajor = AkaoFile(CurPos + &H8)
        .VersionMinor = AkaoFile(CurPos + &H9)
        Call CopyMemory(.Unknown1, AkaoFile(CurPos + &HA), &H4)
        Call CopyMemory(.Unknown2, AkaoFile(CurPos + &HE), &H2)
        CurPos = CurPos + &H10
        
        ' Version Major doens't seem to be the REAL version number
        'If .VersionMajor <= &H3 Then
        '    FileVer = &H1
        'ElseIf .VersionMajor >= &H4 Then
        '    FileVer = &H2
        'End If
        If .Unknown2 = &H0 Then
            FileVer = &H2
        Else
            FileVer = &H1
        End If
        
        Select Case FileVer
        Case &H1
            AkaoLen = &H10 + .FrameLength
        Case &H2
            AkaoLen = .FrameLength
        End Select
    End With
    
    With AkaoDInf
        Select Case FileVer
        Case &H1
            Call CopyMemory(.ChnMask, AkaoFile(CurPos), &H4)
            CurPos = CurPos + &H4
            
            ' Read Channel Mask
            ChnCount = &H0
            For CurChn = &H0 To &H1F
                If CBool(.ChnMask And BIT_SHIFT(CurChn)) Then
                    ChnCount = ChnCount + &H1
                End If
            Next CurChn
            
            ' Read Channel Addresses
            ReDim ChnPos(&H0 To ChnCount)
            For CurChn = &H0 To ChnCount - 1
                Call CopyMemory(TempInt, AkaoFile(CurPos), &H2)
                .ChnOffset(CurChn) = TempInt And &HFFFF&
                ChnPos(CurChn) = CurPos + &H2 + .ChnOffset(CurChn)
                CurPos = CurPos + &H2
            Next CurChn
        Case &H2
            Call CopyMemory(.Unknown1, AkaoFile(CurPos + &H0), &H4)
            Call CopyMemory(.Unknown2, AkaoFile(CurPos + &H4), &H4)
            CurPos = CurPos + &H10
            Call CopyMemory(.ChnMask, AkaoFile(CurPos), &H4)
            CurPos = CurPos + &H10
            Call CopyMemory(.InsBlkAddr, AkaoFile(CurPos + &H0), &H4)
            If .InsBlkAddr Then
                .InsBlkAddr = CurPos + &H0 + .InsBlkAddr
            End If
            Call CopyMemory(.UnknwAddr, AkaoFile(CurPos + &H4), &H4)
            If .UnknwAddr Then
                .UnknwAddr = CurPos + &H4 + .UnknwAddr
            End If
            CurPos = CurPos + &H10
            
            ' Read Channel Mask
            ChnCount = &H0
            ' There are only 24 channels, but Chrono Cross: The Brink of Death
            ' uses 31 channels
            For CurChn = &H0 To &H1F
                If CBool(.ChnMask And BIT_SHIFT(CurChn)) Then
                    ChnCount = ChnCount + &H1
                End If
            Next CurChn
            
            ' Read Channel Addresses
            ReDim ChnPos(&H0 To ChnCount)
            For CurChn = &H0 To ChnCount - 1
                Call CopyMemory(TempInt, AkaoFile(CurPos), &H2)
                .ChnOffset(CurChn) = TempInt And &HFFFF&
                ChnPos(CurChn) = CurPos + .ChnOffset(CurChn)
                CurPos = CurPos + &H2
            Next CurChn
            
            ' Read Instrument Block
            ' ---------------------
            If .InsBlkAddr Then
                ' Count Instruments
                CurPos = .InsBlkAddr
                AkaoIns.InsCount = &H0
                Do
                    Call CopyMemory(TempInt, AkaoFile(CurPos), &H2)
                    If TempInt = &HFFFF Then Exit Do
                    If TempInt = &H0 And AkaoIns.InsCount > &H0 Then Exit Do
                    CurPos = CurPos + &H2
                    AkaoIns.InsCount = AkaoIns.InsCount + &H1
                Loop
                
                ' Read Instrument Set Addresses
                TrkPos = CurPos
                CurPos = .InsBlkAddr
                With AkaoIns
                    ReDim .InsAddr(&H0 To .InsCount - 1)
                    For CurChn = &H0 To .InsCount - 1
                        Call CopyMemory(TempInt, AkaoFile(CurPos), &H2)
                        .InsAddr(CurChn) = TrkPos + TempInt And &HFFFF&
                        CurChn = CurChn + &H1
                        CurPos = CurPos + &H2
                    Next CurChn
                    
                    ' Read Instrument Sets
                    ReDim .InsSet(&H0 To .InsCount - 1)
                    For CurChn = &H0 To .InsCount - 1
                        ' Read Instrument Set Elements
                        CurPos = .InsAddr(CurChn)
                        TempByt = &H0
                        Do
                            Call CopyMemory(TempLng, AkaoFile(CurPos + &H0), &H4)
                            Call CopyMemory(DelayPos, AkaoFile(CurPos + &H4), &H4)
                            If TempLng = &H0 And TempLng = &H0 Then Exit Do
                            
                            TempByt = TempByt + &H1
                            CurPos = CurPos + &H8
                        Loop
                        
                        CurPos = .InsAddr(CurChn)
                        With .InsSet(CurChn)
                            .DataCount = TempByt
                            If .DataCount > &H0 Then
                                ReDim .Data(&H0 To .DataCount - 1)
                                
                                For TempByt = &H0 To .DataCount - 1
                                    Call CopyMemory(.Data(TempByt), AkaoFile(CurPos), &H8)
                                    CurPos = CurPos + &H8
                                Next TempByt
                            Else
                                ReDim .Data(&H0 To &H0)
                            End If
                        End With
                    Next CurChn
                    
                End With
            End If
        End Select
        ChnPos(ChnCount) = PosStart + AkaoLen
    End With
    
    'ChnCount = &H1
    CurPos = CurPos + &H4
    Call CopyMemory(MidiFile(&H0), &H6468544D, &H4)
    Call CopyMemory(MidiFile(&H4), &H6000000, &H4)
    Call CopyMemory(MidiFile(&H8), &H100, &H2)
    MidiFile(&HA) = &H0
    MidiFile(&HB) = ChnCount
    Call CopyMemory(MidiFile(&HC), &H6000, &H2)
    WholeDelay = &H60 * 4
    MidiPos = &HE
    
    HadDlyOvr = False
    For CurChn = &H0 To ChnCount - 1
        'CurChn = 2
        Call CopyMemory(MidiFile(MidiPos + &H0), &H6B72544D, &H4)
        Call CopyMemory(MidiFile(MidiPos + &H4), &HFFFFFFFF, &H4)
        MidiPos = MidiPos + &H8
        
        TrkPos = MidiPos
        CurPos = ChnPos(CurChn)
        
        If CurChn = &H0 And FileVer = &H2 And False Then
            TempLng = 1000000 / (AkaoDInf.Unknown2 / 60)
            
            Call WriteMidiDelay
            MidiFile(MidiPos + &H0) = &HFF
            MidiFile(MidiPos + &H1) = &H51
            MidiFile(MidiPos + &H2) = &H3
            ReDim TempArr(&H0 To &H2)
            Call CopyMemory(TempArr(&H0), TempLng, &H3)
            TempArr() = ReverseBytes(TempArr())
            Call CopyMemory(MidiFile(MidiPos + &H3), TempArr(&H0), &H3)
            MidiPos = MidiPos + &H6
        End If
        
        MidiChn = CurChn And &HF
        If MidiChn = &H9 Then MidiChn = &HF
        'MidiDelay = WholeDelay  ' 4 Quarters Delay
        MidiDelay = 0
        DelayPos = 0
        LastNote = &H0
        CurOctave = &H0
        DrumMode = False
        OctBakM = &H0
        OctBakD = &H0
        ChnVol = &H7F
        NextDelay = &H0
        DelayOverride = 0
        NextNote = 0
        NoteVol = 100
        LastNoteVol = &HFF
        VolFadeDelay = &H0
        LoopIdx = &H0
        CurLoop(LoopIdx) = &H0
        LoopPos(LoopIdx) = &H0
        MstJmpCnt = &H0
        TrkEnd = False
        Do
            CmdVal = AkaoFile(CurPos + &H0)
DoCommand:
            If CurPos + &H1 < AkaoLen Then
                TempByt = AkaoFile(CurPos + &H1)
            Else
                TempByt = &H0
            End If
            
            CommonCmd = True
            If CmdVal < &HA0 Then
                ' Note/Delay
                NoteVal = Fix(CmdVal / 11)
                TempByt = CmdVal Mod 11
                If NoteVal < 12 Then
                    NoteVal = CurOctave * 12 + NoteVal
                ElseIf NoteVal = 12 Then
                    NoteVal = &HFF  ' Continue Note
                ElseIf NoteVal = 13 Then
                    NoteVal = &H0   ' Stop Note
                Else
                    NoteVal = &H0
                End If
                
                MidCmd = &H90 Or MidiChn
                If NoteVal < &HFF Then
                    If LastNote Then
                        Call WriteMidiCommand(MidCmd, LastNote, &H0)
                    End If
                    If CBool(NoteVal) And CBool(NextNote) Then
                        NoteVal = NoteVal + NextNote
                    End If
                    LastNote = NoteVal
                Else
                    NoteVal = &H0
                End If
                NextNote = 0
                If NoteVal Then
                    'If MidiChn = &H9 Then
                    '    If VolFadeDelay > &H0 Then
                    '        VolFadeCur = DelayPos - VolFadeStart
                    '        TempSng = VolFadeCur / VolFadeDelay
                    '        If TempSng > 1! Then TempSng = 1!
                    '        NoteVol = ChnVol + TempSng * (VolFadeTo - ChnVol)
                    '    Else
                    '        NoteVol = ChnVol
                    '    End If
                    'Else
                    '    NoteVol = &H7F
                    'End If
                    Call WriteMidiCommand(MidCmd, NoteVal, NoteVol)
                End If
                
                If TempByt <= 6 Then
                    TempLng = WholeDelay / 2 ^ TempByt
                ElseIf TempByt > 6 Then
                    TempByt = TempByt - 6
                    'If TempByt = 0 Then TempByt = 4
                    TempLng = (WholeDelay / 2 ^ TempByt) / 3
                End If
                If NextDelay = &H1 Then
                    TempLng = TempLng * 1.5
                ElseIf NextDelay = &H2 Then
                    TempLng = TempLng * 2
                ElseIf NextDelay = &H3 Then
                    TempLng = DelayOverride
                ElseIf NextDelay = &HFF Then
                    TempLng = &H0
                End If
                NextDelay = &H0
                If VolFadeDelay = &H0 Then
                    MidiDelay = MidiDelay + TempLng
                    DelayPos = DelayPos + TempLng
                ElseIf VolFadeDelay > &H0 Then
                    If TempLng >= WholeDelay / 32 Then
                        TempByt = &H2
                    ElseIf TempLng >= WholeDelay / 192 Then
                        TempByt = &H1
                    Else
                        TempByt = &H0
                    End If
                    Do While TempLng > &H0
                        If TempByt = &H2 Then
                            TempInt = WholeDelay / 32
                        ElseIf TempByt = &H1 Then
                            TempInt = WholeDelay / 192
                        End If
                        If TempLng < TempInt Then
                            TempInt = TempLng
                        End If
                        MidiDelay = MidiDelay + TempInt
                        DelayPos = DelayPos + TempInt
                        TempLng = TempLng - TempInt
                        
                        VolFadeCur = DelayPos - VolFadeStart
                        TempSng = VolFadeCur / VolFadeDelay
                        If TempSng > 1! Then TempSng = 1!
                        TempSng = Int(TempSng * (VolFadeTo - ChnVol) + 0.5)
                        CmdVal = ChnVol + TempSng
                        'If MidiChn = &H9 Then NoteVol = &H7F
                        If CmdVal <> LastNoteVol Then
                            Call WriteMidiCommand(&HB0 Or MidiChn, &H7, CmdVal)
                            LastNoteVol = CmdVal
                        End If
                        
                        VolFadeCur = DelayPos - VolFadeStart
                        If Abs(VolFadeCur / VolFadeDelay) >= 1 Then
                            ChnVol = VolFadeTo
                            VolFadeDelay = &H0
                            MidiDelay = MidiDelay + TempLng
                            DelayPos = DelayPos + TempLng
                            TempLng = &H0
                        End If
                    Loop
                End If
                
                If NextDelay = &H80 Then
                    Call WriteMidiCommand(&HB0 Or MidiChn, &H41, &H0)
                End If
                
                CurPos = CurPos + &H1
            Else
                Select Case CmdVal
                Case &HA0   ' Finish Channel
                    TrkEnd = True
                    CurPos = CurPos + &H1
                Case &HA1   ' Load Instrument
                    MidCmd = &HC0 Or MidiChn
                    Call WriteMidiCommand(MidCmd, TempByt And &H7F, &H0)
                    If CBool(TempByt And &H80) Then
                        Debug.Print "High Instrument Value: " & Hex$(TempByt)
                    End If
                    CurPos = CurPos + &H2
                Case &HA2   ' "set next note length" according to VGMTrans
                    'If TempByt = &H1 Or TempByt = &H4 Or TempByt = &H2 Then
                    '    ' fixes "Electric de Chocobo", "Interrupted by Fireworks"
                    '    ' but breaks JENOVA
                    '    ' ... don't know why I have to skip this byte
                    '    CurPos = CurPos + &H3
                    'ElseIf TempByt = &H5 Then
                    '    NextDelay = &H2
                    '    CurPos = CurPos + &H2
                    'Else
                    '    CurPos = CurPos + &H2
                    'End If
                    ' This ... seems to look good!
                    NextDelay = &H3
                    DelayOverride = (WholeDelay / 192) * TempByt
                    CurPos = CurPos + &H2
                    'If Not SurpressDbgCtrls Then
                    '    MidCmd = &HB0 Or MidiChn
                    '    Call WriteMidiCommand(MidCmd, &H71, CmdVal And &H7F)
                    '    Call WriteMidiCommand(MidCmd, &H6, TempByt And &H7F)
                    'End If
                    'Call WriteMidiCommand(&HD0 Or MidiChn, &H6, TempByt And &H7F)
                    If Not HadDlyOvr Then
                        HadDlyOvr = True
                        Debug.Print "Used flag A2"
                    End If
                Case &HA3   ' Volume Modifier
                'Case &HA8   ' Volume Modifier
                    NoteVol = TempByt
                    If NoteVol = &H0 Then NoteVol = &H1
                    'MidCmd = &HB0 Or MidiChn
                    'Call WriteMidiCommand(MidCmd, &HB, TempByt)
                    CurPos = CurPos + &H2
                Case &HA4   ' Pitch to Note
                    ' Argument 1:  Pitch-Time
                    ' Argument 2:  Relative Note
                    TempByt = AkaoFile(CurPos + &H2)
                    TempInt = IIf(TempByt And &H80, TempByt Or &HFF00, TempByt)
                    TempByt = AkaoFile(CurPos + &H1)
                    If LastNote Then
                        ' in FF7: You Can Hear the Cry of the Planet
                        ' LastNote occours to be 0
                        NoteVal = LastNote + TempInt
                    End If
                    
                    MidCmd = &HB0 Or MidiChn
                    'Call WriteMidiCommand(MidCmd, &H41, &H7F)
                    Call WriteMidiCommand(MidCmd, &H5, TempByt)
                    
                    If LastNote Then
                        MidCmd = &H90 Or MidiChn
                        Call WriteMidiCommand(MidCmd, LastNote, &H0)
                        Call WriteMidiCommand(MidCmd, NoteVal, NoteVol)
                        LastNote = NoteVal
                    End If
                    CurPos = CurPos + &H3
                Case &HA5   ' Pitch Divider / Octave
                    ' this is in the range from 00 .. 0F
                    ' I'm sure of this because in AKAOv2 Instrument Blocks
                    ' the High Note is B3 (a B in Octave 15)
                    CurOctave = TempByt
                    CurPos = CurPos + &H2
                Case &HA6   ' Octave Up
                    CurOctave = CurOctave + 1
                    If CurOctave > 8 Then Stop
                    CurPos = CurPos + &H1
                Case &HA7   ' Octave Down
                    If CurOctave > 0 Then
                    CurOctave = CurOctave - 1
                    End If
                    CurPos = CurPos + &H1
                Case &HA8   ' Channel Volume
                'Case &HA3   ' Channel Volume
                    'TempByt = Int(TempByt / &H2) + &H40
                    'MidCmd = &HB0 Or MidiChn
                    'Call WriteMidiCommand(MidCmd, &HB, TempByt)
                    ChnVol = TempByt
                    'If MidiChn = &H9 Then
                    '    If ChnVol = &H0 Then ChnVol = &H1
                    '    NoteVol = &H7F
                    'Else
                    '    NoteVol = ChnVol
                    'End If
                    Call WriteMidiCommand(&HB0 Or MidiChn, &H7, ChnVol)
                    CurPos = CurPos + &H2
                Case &HA9   ' Volume Fading
                    ' Argument 1 - Fading Time
                    ' Argument 2 - Volume
                    VolFadeDelay = AkaoFile(CurPos + &H1) * (WholeDelay / 192)
                    VolFadeTo = AkaoFile(CurPos + &H2)
                    VolFadeCur = &H0
                    VolFadeStart = DelayPos
                    
                    MidCmd = &HB0 Or MidiChn
                    'Call WriteMidiCommand(MidCmd, &H27, TempByt And &HFF)
                    CurPos = CurPos + &H3
                Case &HAA   ' Channel Pan
                    MidCmd = &HB0 Or MidiChn
                    Call WriteMidiCommand(MidCmd, &HA, TempByt)
                    CurPos = CurPos + &H2
                Case &HAB
                    CurPos = CurPos + &H3
                    GoTo WriteUnknownCommand
                Case &HAD
                    CurPos = CurPos + &H2
                    GoTo WriteUnknownCommand
                Case &HAE
                    CurPos = CurPos + &H2
                    GoTo WriteUnknownCommand
                Case &HAF
                    CurPos = CurPos + &H2
                    GoTo WriteUnknownCommand
                Case &HB1
                    CurPos = CurPos + &H2
                    GoTo WriteUnknownCommand
                Case &HB4   ' Unknown
                    CurPos = CurPos + &H4
                    GoTo WriteUnknownCommand
                Case &HB5
                    CurPos = CurPos + &H2
                    GoTo WriteUnknownCommand
                Case &HBC
                    CurPos = CurPos + &H3
                    GoTo WriteUnknownCommand
                Case &HBD
                    CurPos = CurPos + &H2
                    GoTo WriteUnknownCommand
                Case &HC2   ' Turn On Reverb
                    If Not SurpressDbgCtrls Then
                        MidCmd = &HB0 Or MidiChn
                        Call WriteMidiCommand(MidCmd, &H5A, &H7F)
                    End If
                    CurPos = CurPos + &H1
                Case &HC3   ' Turn Off Reverb ??
                    If Not SurpressDbgCtrls Then
                        MidCmd = &HB0 Or MidiChn
                        Call WriteMidiCommand(MidCmd, &H5A, &H0)
                    End If
                    CurPos = CurPos + &H1
                Case &HC8   ' Loop Point
                    MidCmd = &HB0 Or MidiChn
                    If Not SurpressDbgCtrls Then
                        Call WriteMidiCommand(MidCmd, &H3, LoopIdx)
                        Call WriteMidiCommand(MidCmd, &H23, &H0)
                    End If
                    
                    If LoopPos(LoopIdx) <> &H0 Then
                        LoopIdx = LoopIdx + &H1
                    End If
                    CurLoop(LoopIdx) = &H0
                    LoopPos(LoopIdx) = CurPos
                    CurPos = CurPos + &H1
                Case &HC9   ' Loop End
                    CurLoop(LoopIdx) = CurLoop(LoopIdx) + &H1
                    
                    MidCmd = &HB0 Or MidiChn
                    If Not SurpressDbgCtrls Then
                        Call WriteMidiCommand(MidCmd, &H3, LoopIdx)
                        Call WriteMidiCommand(MidCmd, &H23, CurLoop(LoopIdx))
                    End If
                    
                    If CurLoop(LoopIdx) < TempByt Then
                        If LoopPos(LoopIdx) = &H0 Then Stop
                        CurPos = LoopPos(LoopIdx) + &H1
                    Else
                        CurPos = CurPos + &H2
                        LoopPos(LoopIdx) = &H0
                        CurLoop(LoopIdx) = &H0
                        If LoopIdx > &H0 Then
                            LoopIdx = LoopIdx - &H1
                        End If
                    End If
                Case &HCA   ' Return to Loop Point
                    MidCmd = &HB0 Or MidiChn
                    If Not SurpressDbgCtrls Then
                        Call WriteMidiCommand(MidCmd, &H3, LoopIdx)
                        Call WriteMidiCommand(MidCmd, &H23, &H7F)
                    End If
                    
                    CurPos = CurPos + &H1
                Case &HD8
                    CurPos = CurPos + &H2
                    GoTo WriteUnknownCommand
                Case &HDD
                    CurPos = CurPos + &H3
                    GoTo WriteUnknownCommand
                Case &HEA   ' Reverb Depth
                    TempLng = &H0
                    Call CopyMemory(TempLng, AkaoFile(CurPos + &H1), &H2)
                    TempByt = TempLng / &H100
                    MidCmd = &HB0 Or MidiChn
                    Call WriteMidiCommand(MidCmd, &H5B, TempByt)
                    
                    CurPos = CurPos + &H3
                Case Else
                    CommonCmd = False
                End Select
            End If
            If Not CommonCmd Then
                Select Case FileVer
                Case &H1
                    Select Case CmdVal
                    Case &HB2
                        CurPos = CurPos + &H2
                        GoTo WriteUnknownCommand
                    Case &HB6
                        CurPos = CurPos + &H1
                        GoTo WriteUnknownCommand
                    Case &HB8
                        CurPos = CurPos + &H4
                        GoTo WriteUnknownCommand
                    Case &HB9
                        MidCmd = &HB0 Or MidiChn
                        Call WriteMidiCommand(MidCmd, &H27, TempByt)
                        CurPos = CurPos + &H2
                        'GoTo WriteUnknownCommand
                    Case &HBA
                        CurPos = CurPos + &H2
                        GoTo WriteUnknownCommand
                    Case &HC0
                        CurPos = CurPos + &H2
                        GoTo WriteUnknownCommand
                    Case &HC6
                        CurPos = CurPos + &H1
                        GoTo WriteUnknownCommand
                    Case &HC7
                        CurPos = CurPos + &H1
                        GoTo WriteUnknownCommand
                    Case &HCC
                        CurPos = CurPos + &H1
                        GoTo WriteUnknownCommand
                    Case &HCD   ' Unimplemented
                        CurPos = CurPos + &H1
                        GoTo WriteUnknownCommand
                    Case &HD0
                        CurPos = CurPos + &H1
                        GoTo WriteUnknownCommand
                    Case &HD1   ' Unimplemented
                        CurPos = CurPos + &H1
                        GoTo WriteUnknownCommand
                    Case &HD4
                        CurPos = CurPos + &H2
                        GoTo WriteUnknownCommand
                    Case &HDE
                        CurPos = CurPos + &H3
                        GoTo WriteUnknownCommand
                    Case &HE8   ' Tempo
                        TempLng = &H0
                        Call CopyMemory(TempLng, AkaoFile(CurPos + &H1), &H2)
                        TempSng = TempLng / 219             ' AKAO -> BPM
                        TempLng = 1000000 / (TempSng / 60)  ' BPM -> MIDI
                        
                        Call WriteMidiDelay
                        MidiFile(MidiPos + &H0) = &HFF
                        MidiFile(MidiPos + &H1) = &H51
                        MidiFile(MidiPos + &H2) = &H3
                        ReDim TempArr(&H0 To &H2)
                        Call CopyMemory(TempArr(&H0), TempLng, &H3)
                        TempArr() = ReverseBytes(TempArr())
                        Call CopyMemory(MidiFile(MidiPos + &H3), TempArr(&H0), &H3)
                        MidiPos = MidiPos + &H6
                        
                        CurPos = CurPos + &H3
                    Case &HEC   ' Drum Channel On
                        ' Arguments: 2-byte Pointer to the End of the last Track
                        ' The Pitch Divider for Drums seems to be stored seperately
                        If Not DrumMode Then
                            OctBakM = CurOctave
                        Else
                            OctBakD = CurOctave
                        End If
                        CurOctave = OctBakD
                        DrumMode = True
                        
                        MidiChn = &H9
                        CurPos = CurPos + &H3
                        GoTo WriteUnknownCommand
                    Case &HED   ' Drum Channel Off
                        If Not DrumMode Then
                            OctBakM = CurOctave
                        Else
                            OctBakD = CurOctave
                        End If
                        CurOctave = OctBakM
                        DrumMode = False
                        
                        MidiChn = CurChn And &HF
                        If MidiChn = &H9 Then MidiChn = &HF
                        CurPos = CurPos + &H1
                        GoTo WriteUnknownCommand
                    Case &HEE   ' Jump
                        ' Used as Master Loop
                        Call CopyMemory(TempInt, AkaoFile(CurPos + &H1), &H2)
                        
                        MidCmd = &HB0 Or MidiChn
                        If TempInt = &H0 Then Stop
                        If TempInt < &H0 Then
                            MstJmpCnt = MstJmpCnt + &H1
                            If MstJmpCnt >= &H2 Then
                                TrkEnd = True
                            End If
                            'If Not SurpressDbgCtrls Then
                                Call WriteMidiCommand(MidCmd, &H6F, MstJmpCnt)
                            'End If
                            If LastNote Then
                                MidCmd = &H90 Or MidiChn
                                Call WriteMidiCommand(MidCmd, LastNote, &H0)
                                LastNote = &H0
                            End If
                        Else
                            Debug.Print "Jump to " & Format$(TempInt)
                            CurPos = CurPos + &H3
                            TempByt = AkaoFile(CurPos + &H2)
                            If Not SurpressDbgCtrls Then
                                Call WriteMidiCommand(MidCmd, &H9, TempByt And &HFF)
                            End If
                        End If
                        CurPos = CurPos + &H3 + TempInt
                    Case &HF0   ' Jump out of Loop ??
                        If CurLoop(LoopIdx) + &H1 = TempByt Then
                            MidCmd = &HB0 Or MidiChn
                            If Not SurpressDbgCtrls Then
                                Call WriteMidiCommand(MidCmd, &H3, LoopIdx)
                                Call WriteMidiCommand(MidCmd, &H23, &H7F)
                            End If
                            
                            TempLng = &H0
                            Call CopyMemory(TempLng, AkaoFile(CurPos + &H2), &H2)
                            CurPos = CurPos + TempLng
                            LoopPos(LoopIdx) = &H0
                            CurLoop(LoopIdx) = &H0
                            If LoopIdx > &H0 Then
                                LoopIdx = LoopIdx - &H1
                            End If
                        End If
                        CurPos = CurPos + &H4
                    Case &HF1   ' Special Setting ???
                        CurPos = CurPos + &H4
                        GoTo WriteUnknownCommand
                    Case &HF2
                        CurPos = CurPos + &H2
                        GoTo WriteUnknownCommand
                    Case &HF4
                        CurPos = CurPos + &H3
                        GoTo WriteUnknownCommand
                    Case &HF5
                        CurPos = CurPos + &H1
                        GoTo WriteUnknownCommand
                    Case &HF6
                        CurPos = CurPos + &H2
                        GoTo WriteUnknownCommand
                    Case &HF7
                        CurPos = CurPos + &H3
                        GoTo WriteUnknownCommand
                    Case &HF8
                        CurPos = CurPos + &H2
                        GoTo WriteUnknownCommand
                    Case &HF9
                        CurPos = CurPos + &H1
                        GoTo WriteUnknownCommand
                    Case &HFD   ' Unknown
                        CurPos = CurPos + &H3
                        GoTo WriteUnknownCommand
                    Case &HFE  ' Unknown
                        CurPos = CurPos + &H3
                        GoTo WriteUnknownCommand
                    Case &HE0 To &HE7, &HFA To &HFC, &HFF
                        ' Unimplemented Commands (Finish Channel)
                        TrkEnd = True
                        CurPos = CurPos + &H1
                    Case Else
                        Debug.Print "Ch " & Format$(CurChn) & _
                                    ": Unknown Command " & Hex$(CmdVal)
                        MidCmd = &HB0 Or MidiChn
                        If Not SurpressDbgCtrls Then
                            Call WriteMidiCommand(MidCmd, &H70, CmdVal And &H7F)
                        End If
                        'TrkEnd = True
                        'Stop
                        CurPos = CurPos + &H1
                    End Select
                Case &H2
                    Select Case CmdVal
                    Case &HCC
                        CurPos = CurPos + &H1
                        GoTo WriteUnknownCommand
                    Case &HCD   ' Unimplemented
                        CurPos = CurPos + &H1
                        GoTo WriteUnknownCommand
                    Case &HD0
                        CurPos = CurPos + &H1
                        GoTo WriteUnknownCommand
                    Case &HD1   ' Unimplemented
                        CurPos = CurPos + &H1
                        GoTo WriteUnknownCommand
                    Case &HF0 To &HF9
                        NextDelay = &H1
                        If TempByt >= &H84 Then
                            NextNote = CmdVal - &HF3
                        ElseIf TempByt = &H48 Then
                            NextDelay = &H1
                            NextNote = 0
                            'CurPos = CurPos + &H1
                        Else
                            NextDelay = &H0
                            TempLng = (WholeDelay / 192) * TempByt
                            MidiDelay = MidiDelay + TempLng
                        End If
                        CurPos = CurPos + &H1
                        GoTo WriteUnknownCommand
                    Case &HF9
                        CurPos = CurPos + &H2
                        GoTo WriteUnknownCommand
                    Case &HFA To &HFB
                        'If Not SurpressDbgCtrls Then
                        '    MidCmd = &HB0 Or MidiChn
                        '    Call WriteMidiCommand(MidCmd, &H71, CmdVal And &H7F)
                        '    Call WriteMidiCommand(MidCmd, &H26, TempByt)
                        'End If
                        
                        'NextDelay = &H1
                        'NextNote = CmdVal - &HF3
                        TempLng = (WholeDelay / 192) * TempByt
                        MidiDelay = MidiDelay + TempLng
                        CurPos = CurPos + &H2
                        GoTo WriteUnknownCommand
                    Case &HFC, &HFD
                        If Not SurpressDbgCtrls Then
                            MidCmd = &HB0 Or MidiChn
                            Call WriteMidiCommand(MidCmd, &H71, CmdVal And &H7F)
                        End If
                        
                        If TempByt < &HA0 Then
                            TempLng = (WholeDelay / 192) * TempByt
                            MidiDelay = MidiDelay + TempLng
                            DelayPos = DelayPos + TempLng
                            CurPos = CurPos + &H2
                        Else
                            CurPos = CurPos + &H1
                        End If
                        
                        If Not SurpressDbgCtrls Then
                            Call WriteMidiCommand(MidCmd, &H26, TempByt)
                        End If
                    Case &HFD
                        If Not SurpressDbgCtrls Then
                            MidCmd = &HB0 Or MidiChn
                            Call WriteMidiCommand(MidCmd, &H71, CmdVal And &H7F)
                        End If
                        
                        If TempByt < &HA0 Then
                            If TempByt < 11 Then
                                TempInt = 2 ^ (TempByt + 4)
                                TempInt = WholeDelay / TempInt
                                TempLng = -Int(-DelayPos / TempInt) * TempInt - DelayPos
                                'TempLng = 0
                            Else
                                TempLng = TempByt * 2
                            End If
                            MidiDelay = MidiDelay + TempLng
                            DelayPos = DelayPos + TempLng
                            CurPos = CurPos + &H2
                        Else
                            CurPos = CurPos + &H1
                        End If
                        
                        If Not SurpressDbgCtrls Then
                            Call WriteMidiCommand(MidCmd, &H26, TempByt)
                        End If
                    Case &HFE  ' Unknown
                        If TempByt >= &HA0 Then
                            Stop
                            CurPos = CurPos + &H1
                            CmdVal = AkaoFile(CurPos + &H0)
                            GoTo DoCommand
                        End If
                        Select Case TempByt
                        Case &H0    ' Tempo??
                            ' Arguments:
                            '   Byte 00-01 - Tempo Value (&H4000 = 90? BPM)
                            TempLng = &H0
                            Call CopyMemory(TempLng, AkaoFile(CurPos + &H2), &H2)
                            TempSng = TempLng * 75 / &H4000     ' AKAO -> BPM
                            TempLng = 1000000 / (TempSng / 60)  ' BPM -> MIDI
                            
                            Call WriteMidiDelay
                            MidiFile(MidiPos + &H0) = &HFF
                            MidiFile(MidiPos + &H1) = &H51
                            MidiFile(MidiPos + &H2) = &H3
                            ReDim TempArr(&H0 To &H2)
                            Call CopyMemory(TempArr(&H0), TempLng, &H3)
                            TempArr() = ReverseBytes(TempArr())
                            Call CopyMemory(MidiFile(MidiPos + &H3), TempArr(&H0), &H3)
                            MidiPos = MidiPos + &H6
                            
                            CurPos = CurPos + &H4
                        Case &H1
                            ' Arguments:
                            '   Byte 00 - Unknown
                            Stop
                            CurPos = CurPos + &H3
                        Case &H2    ' Set Reverb Depth
                            ' Arguments:
                            '   Byte 00 - Unknown (usually 00)
                            '   Byte 01 - Reverb Depth
                            TempByt = AkaoFile(CurPos + &H3)
                            If Not SurpressDbgCtrls Then
                                MidCmd = &HB0 Or MidiChn
                                Call WriteMidiCommand(MidCmd, &H5B, TempByt)
                            End If
                            
                            CurPos = CurPos + &H4
                        Case &H4    ' Drum Channel On
                            ' Arguments:
                            '   None
                            DrumMode = True
                            MidiChn = &H9
                            CurPos = CurPos + &H2
                        Case &H6    ' Jump (Used for Master Loop)
                            ' Arguments:
                            '   Byte 00-01 - relative Jump Address
                            Call CopyMemory(TempInt, AkaoFile(CurPos + &H2), &H2)
                            
                            MidCmd = &HB0 Or MidiChn
                            If TempInt = &H0 Then Stop
                            If TempInt < &H0 Then
                                MstJmpCnt = MstJmpCnt + &H1
                                If MstJmpCnt >= &H2 Then
                                    TrkEnd = True
                                End If
                                'If Not SurpressDbgCtrls Then
                                    Call WriteMidiCommand(MidCmd, &H6F, MstJmpCnt)
                                'End If
                                If LastNote Then
                                    MidCmd = &H90 Or MidiChn
                                    Call WriteMidiCommand(MidCmd, LastNote, &H0)
                                    LastNote = &H0
                                End If
                            Else
                                Debug.Print "Jump to " & Format$(TempInt)
                                CurPos = CurPos + &H3
                                TempByt = AkaoFile(CurPos + &H2)
                                If Not SurpressDbgCtrls Then
                                    Call WriteMidiCommand(MidCmd, &H9, TempByt And &HFF)
                                End If
                            End If
                            LoopIdx = &H0
                            CurPos = CurPos + &H2 + TempInt
                        Case &HB
                            ' Arguments:
                            '   Byte 00 - Unknown
                            '   Byte 01 - Unknown
                            Call CopyMemory(TempInt, AkaoFile(CurPos + &H2), &H2)
                            Debug.Print Hex(&HFE) & " " & Hex(TempByt) & " " & Hex(TempInt)
                            CurPos = CurPos + &H4
                        Case &H14   ' Instrument Change
                            ' Arguments:
                            '   Byte 00 - Instrument Number (see Instrument Block)
                            MidCmd = &HC0 Or MidiChn
                            TempByt = AkaoFile(CurPos + &H2)
                            Call WriteMidiCommand(MidCmd, TempByt And &H7F, &H0)
                            If CBool(TempByt And &H80) Then
                                Debug.Print "High Instrument Value: " & Hex$(TempByt)
                            End If
                            CurPos = CurPos + &H3
                        Case &H15
                            ' Arguments:
                            '   Byte 00 - Unknown (usually 30h)
                            '   Byte 01 - Unknown
                            Call CopyMemory(TempInt, AkaoFile(CurPos + &H2), &H2)
                            Debug.Print Hex(&HFE) & " " & Hex(TempByt) & " " & Hex(TempInt)
                            CurPos = CurPos + &H4
                        Case &H16
                            ' Arguments:
                            '   Byte 00 - Unknown (mostly 10h)
                            '   Byte 01 - Unknown (usually 00h)
                            Call CopyMemory(TempInt, AkaoFile(CurPos + &H2), &H2)
                            Debug.Print Hex(&HFE) & " " & Hex(TempByt) & " " & Hex(TempInt)
                            CurPos = CurPos + &H4
                        Case &H1D
                            ' Arguments:
                            '   None
                            CurPos = CurPos + &H2
                        Case &H8F
                            ' Arguments:
                            '   Byte 00 - Unknown
                            CurPos = CurPos + &H3
                            Stop
                        Case Else
                            Stop
                            Call CopyMemory(TempInt, AkaoFile(CurPos + &H2), &H2)
                            Debug.Print Hex(&HFE) & " " & Hex(TempByt) & " " & Hex(TempInt)
                            CurPos = CurPos + &H4
                        End Select
                        GoTo WriteUnknownCommand
                    ' Unimplemented Commands (Finish Channel)
                    'Case &HFF
                    '    TrkEnd = True
                    '    CurPos = CurPos + &H1
                    Case Else
                        Debug.Print "Ch " & Format$(CurChn) & _
                                    ": Unknown Command " & Hex$(CmdVal)
                        If Not SurpressDbgCtrls Then
                            MidCmd = &HB0 Or MidiChn
                            Call WriteMidiCommand(MidCmd, &H70, CmdVal And &H7F)
                        End If
                        'TrkEnd = True
                        'Stop
                        CurPos = CurPos + &H1
                    End Select
                End Select
            End If
            If False Then
WriteUnknownCommand:
                If Not SurpressDbgCtrls Then
                    MidCmd = &HB0 Or MidiChn
                    Call WriteMidiCommand(MidCmd, &H71, CmdVal And &H7F)
                End If
            End If
            
            ' Forcing Track End doesn't work with FF7: JENOVA
            ' because of shuffled tracks
            'If Not TrkEnd And CurPos >= ChnPos(CurChn + &H1) Then
            '    Debug.Print "Track End Ch " & Hex$(CurChn) & " Force: " & Hex$(CmdVal)
            '    TrkEnd = True
            'End If
            If CurPos >= AkaoLen Then TrkEnd = True
        Loop Until TrkEnd
        
        If LastNote Then
            MidCmd = &H90 Or MidiChn
            Call WriteMidiCommand(MidCmd, LastNote, &H0)
        End If
        Call WriteMidiDelay
        MidiFile(MidiPos + &H0) = &HFF
        MidiFile(MidiPos + &H1) = &H2F
        MidiFile(MidiPos + &H2) = &H0
        MidiPos = MidiPos + &H3
        
        TempLng = MidiPos - TrkPos
        ReDim TempArr(&H0 To &H3)
        Call CopyMemory(TempArr(&H0), TempLng, &H4)
        TempArr() = ReverseBytes(TempArr())
        Call CopyMemory(MidiFile(TrkPos - &H4), TempArr(&H0), &H4)
    Next CurChn
    MidiLen = MidiPos
    
    ReDim Preserve MidiFile(&H0 To MidiLen - 1)
    
    PosStart = CurPos
    Akao2MidConversion = MidiLen

End Function

Private Function ReverseBytes(ByRef SourceArr() As Byte) As Byte()

    Dim TempArr() As Byte
    Dim ByteCount As Long
    Dim CurByte As Long
    
    ByteCount = UBound(SourceArr()) + 1
    ReDim TempArr(&H0 To ByteCount - 1)
    For CurByte = &H0 To ByteCount - 1
        TempArr(ByteCount - 1 - CurByte) = SourceArr(CurByte)
    Next CurByte
    
    ReverseBytes = TempArr()

End Function

Private Sub WriteMidiDelay()

    Dim DataString As String
    Dim TempVal As Byte
    Dim TempDelay As Long
    Dim DelayArr(&H0 To &HF) As Byte
    Dim DelayPos As Byte
    
    DelayPos = &H0
    
    TempVal = MidiDelay And &H7F
    TempDelay = Int(MidiDelay / &H80)
    DelayArr(DelayPos) = TempVal
    DelayPos = DelayPos + &H1
    
    Do While TempDelay > &H0
        TempVal = TempDelay And &H7F
        TempDelay = Int(TempDelay / &H80)
        DelayArr(DelayPos) = TempVal Or &H80
        DelayPos = DelayPos + &H1
    Loop
    
    For TempDelay = DelayPos - &H1 To &H0 Step -1
        MidiFile(MidiPos) = DelayArr(TempDelay)
        MidiPos = MidiPos + &H1
    Next TempDelay
    
    MidiDelay = &H0

End Sub

Private Sub WriteMidiCommand(ByVal CmdVal As Byte, ByVal Param1 As Byte, _
                            ByVal Param2 As Byte)

    If MidiPos + &H20 >= MidiLen Then
        MidiLen = MidiLen + &H40000
        ReDim Preserve MidiFile(&H0 To MidiLen - 1)
    End If
    
    Call WriteMidiDelay
    
    Select Case CmdVal And &HF0
    Case &H80, &H90, &HA0, &HB0, &HE0
        MidiFile(MidiPos + &H0) = CmdVal
        MidiFile(MidiPos + &H1) = Param1
        MidiFile(MidiPos + &H2) = Param2
        MidiPos = MidiPos + &H3
    Case &HC0, &HD0
        MidiFile(MidiPos + &H0) = CmdVal
        MidiFile(MidiPos + &H1) = Param1
        MidiPos = MidiPos + &H2
    End Select

End Sub
