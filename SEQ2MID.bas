Attribute VB_Name = "Module1"
Option Explicit

Private Declare Sub CopyMemory Lib "kernel32" Alias "RtlMoveMemory" _
    (ByRef hpvDest As Any, ByRef hpvSource As Any, ByVal cbCopy As Long)

Private Type FILE_LIST
    FileName As String
    FileType As Byte
End Type

Private Const INS_MAX_INSTRUMENTS = &H80
Private Const INS_DATA_BLK_LINES = &H10
Private Type INS_HEADER
    fccSignature As Long
    dwUnknown04 As Long
    dwUnknown08 As Long
    dwInsSize As Long       ' Instrument Definition + Data/Samples
    wUnknown10 As Integer
    wInsCount As Integer    ' Number of Instument Data Blocks
    wInsLineUsed As Integer
    wInsIDCount As Integer
    bMasterVol As Byte
    bMasterPan As Byte
    bUnknown1A As Byte
    bUnknown1B As Byte
    dwPadding As Long
    ' -> 20 Bytes
End Type
Private Type INS_DESCRIPTION
    bDataCount As Byte  ' Number of Lines
    bInsVolume As Byte
    wUnknown02 As Integer
    bInsPanorama As Byte
    bUnknown05 As Byte
    bUnknown06 As Byte
    bUnknown07 As Byte
    dwPadding08 As Long
    dwPadding0C As Long
    ' -> 10 Bytes
End Type
Private Type INS_DATA_BLOCK
    bUnknown00 As Byte
    bUnknown01 As Byte
    bToneVol As Byte
    bTonePan As Byte
    bUnknown04 As Byte
    bUnknown05 As Byte
    bNoteLow As Byte
    bNoteHigh As Byte
    dwUnknown08 As Long
    bPbDepthMSB As Byte
    bPbDepthLSB As Byte
    wUnknown0E As Integer
    wUnknown10 As Integer
    wUnknown12 As Integer
    wInsNumber As Integer
    wInsID As Integer
    wUnknown18 As Integer
    wUnknown1A As Integer
    wUnknown1C As Integer
    wUnknown1E As Integer
    ' -> 20 Bytes
End Type
Private Type INS_DATA
    DataBlk() As INS_DATA_BLOCK
End Type
Private Type INSTRUMENT_DATA
    Head As INS_HEADER
    Desc() As INS_DESCRIPTION
    Data() As INS_DATA
End Type

' FourCharCode Constants
Private Const FCC_PBAV As Long = &H56414270
Private Const FCC_PQES As Long = &H53455170

' File Type Constants
Private Const FT_FF5 As Byte = &H0
Private Const FT_FF1 As Byte = &H1
Private Const FT_USP As Byte = &HFF

Private FileNameType As Byte
Private FixMidi As Boolean
Private InsertPB As Boolean
Private FixInsSet As Boolean
Private SeqLen As Long
Private SeqFile() As Byte

Sub Main()

    Dim FilePath As String
    Dim FileName As String
    Dim FileType As Byte
    Dim FileList(&H0 To &HF) As FILE_LIST
    Dim MidAllCnt As Long
    Dim MidMnCnt As Long
    Dim MidSubCnt As Long
    Dim CurPos As Long
    Dim MidLen As Long
    Dim MidFile() As Byte
    Dim TempLng As Long
    Dim TempFile As String
    Dim InsData As INSTRUMENT_DATA
    
    ' Works also with uncompressed PSFs
    FileList(&H0).FileName = "FF5_BGM\BGM.BIN"
    FileList(&H0).FileType = FT_FF5
    FileList(&H1).FileName = "FF5_BGM\F5BG44.BIN"
    FileList(&H1).FileType = FT_FF5
    FileList(&H2).FileName = "top 104 harmonious moment.minipsf"
    FileList(&H2).FileType = FT_FF1
    FileList(&H3).FileName = "FF1_BGM\EFFECT.DAT"
    FileList(&H3).FileType = FT_USP ' the data is compressed
    FileList(&H4).FileName = "FF1_BGM\SE.DAT"
    FileList(&H4).FileType = FT_FF1
    FileList(&H5).FileName = "CD\FF1_BGM\BGM_19_135 Last Battle.psf"
    FileList(&H5).FileType = FT_FF1
    'FileList(&H6).FileName = "ToD\ToD_lib.EXE.bin"
    'FileList(&H6).FileName = "ToE\InsSetA\toe 068 dance music.seq"
    FileList(&H6).FileName = "ToE\InsSetA\toe 025 efreet gorge.seq"
    FileList(&H6).FileType = FT_FF1
    
    FixMidi = True
    InsertPB = FixMidi And False
    FixInsSet = FixMidi And False
    FileNameType = &H0
    
    TempLng = &H6
    FileName = "D:\VStudio-Programme\VBasic\PSFCnv\" & FileList(TempLng).FileName
    FileType = FileList(TempLng).FileType
    TempFile = Command$()
    If TempFile <> "" Then
        FileName = TempFile
        If Left$(FileName, 1) = Chr$(&H22) Then
            FileName = Mid$(FileName, 2, Len(FileName) - 2)
        End If
    End If
    If TempLng = &H2 Or TempLng = &H4 Then
        FileNameType = &H2
    End If
    
    Select Case FileType
    Case FT_FF5
        Debug.Print "-- Final Fantasy 5 Sound File --"
    Case FT_FF1
        Debug.Print "-- Final Fantasy 1 Sound File --"
    Case FT_USP
        Debug.Print "Error: Unsupported Sound File"
        Exit Sub
    Case Else
        Debug.Print "File Type Error!"
        Stop
    End Select
    
    If Dir(FileName, vbHidden) = "" Then
        Debug.Print "File not found!"
        Exit Sub
    End If
    
    Debug.Print "Loading ...";
    Open FileName For Binary Access Read As #1
        SeqLen = LOF(1)
        ReDim SeqFile(&H0 To SeqLen - 1)
        Get #1, 1, SeqFile()
    Close #1
    Debug.Print "  OK"
    
    TempLng = InStrRev(FileName, "\")
    FilePath = Left$(FileName, TempLng)
    FileName = Mid$(FileName, TempLng + 1)
    FileName = Left$(FileName, InStrRev(FileName, ".") - 1)
    
    Debug.Print "Converting ..."
    MidAllCnt = 0
    MidMnCnt = -1
    MidSubCnt = 0
    CurPos = &H0
    Do
        Do
            If SeqFile(CurPos) = &H70 Then
                Call CopyMemory(TempLng, SeqFile(CurPos), &H4)
                
                If TempLng = FCC_PBAV Then
                    Call CopyMemory(TempLng, SeqFile(CurPos + &H8), &H4)
                    If TempLng = &H0 Then
                        ' Old code to skip the Instrument Data (Game dependend)
                        'Select Case FileType
                        'Case FT_FF5
                        '    Call CopyMemory(TempLng, SeqFile(CurPos - &H4), &H4)
                        '    TempLng = TempLng - &HC
                        'Case FT_FF1
                        '    Call CopyMemory(TempLng, SeqFile(CurPos + &HC), &H4)
                        'Case Else
                        '    Debug.Print "File Type Error!"
                        '    Stop
                        'End Select
                        'If TempLng < &H0 Or TempLng >= SeqLen Then
                        '    TempLng = &H4
                        'End If
                        MidMnCnt = MidMnCnt + 1
                        MidSubCnt = 0
                        
                        TempLng = Seq2InsConversion(CurPos, MidFile(), InsData)
                        
                        Select Case FileNameType
                        Case &H0
                            TempFile = ""
                        Case &H1
                            TempFile = "_" & Format$(MidAllCnt, "00")
                        Case &H2
                            TempFile = "_" & Format$(MidMnCnt, "00")
                        End Select
                        TempFile = FileName & TempFile & ".ins"
                        Open FilePath & TempFile For Output As #2
                        Close #2
                        Open FilePath & TempFile For Binary Access Write As #2
                            Put #2, 1, MidFile()
                        Close #2
                        
                        CurPos = CurPos + TempLng
                        
                        Call CopyMemory(TempLng, SeqFile(CurPos), &H4)
                    End If
                End If
                If TempLng = FCC_PQES Then
                    Call CopyMemory(TempLng, SeqFile(CurPos + &H4), &H4)
                    If TempLng = &H1000000 And SeqFile(CurPos + &H7) = &H1 Then
                        Exit Do
                    Else
                        CurPos = CurPos + &H4
                    End If
                Else
                    CurPos = CurPos + &H1
                End If
            Else
                CurPos = CurPos + &H1
            End If
        Loop While CurPos < SeqLen
        If CurPos >= SeqLen Then
            Exit Do
        End If
        
        If MidMnCnt = -1 And FileNameType = &H1 Then FileNameType = &H0
        Select Case FileNameType
        Case &H0
            TempFile = ""
        Case &H1
            TempFile = "_" & Format$(MidAllCnt, "00")
        Case &H2
            TempFile = "_" & Format$(MidMnCnt, "00") & "-" & Format$(MidSubCnt, "00")
        End Select
        Debug.Print "File " & Format$(MidAllCnt + 1) & ": " & FileName & TempFile
        TempFile = FileName & TempFile & ".mid"
        
        Call Seq2MidConversion(CurPos, MidFile(), InsData)
        
        Open FilePath & TempFile For Output As #2
        Close #2
        Open FilePath & TempFile For Binary Access Write As #2
            Put #2, 1, MidFile()
        Close #2
        
        MidAllCnt = MidAllCnt + 1
        MidSubCnt = MidSubCnt + 1
    Loop
    
    Debug.Print Format$(MidAllCnt) & " Files saved."

End Sub

Private Function Seq2MidConversion(ByRef PosStart As Long, ByRef RetData() As Byte, _
                                    ByRef InsData As INSTRUMENT_DATA) As Long

    Dim MidLen As Long
    Dim MidFile() As Byte
    Dim CurPos As Long
    Dim MidPos As Long
    Dim TempLng As Long
    Dim TempInt As Integer
    Dim TempByt As Byte
    Dim TempArr() As Byte
    Dim LastEvt As Byte
    Dim MidiEnd As Boolean
    Dim MidRes As Integer
    Dim IntroOn As Boolean
    Dim IntroDelay As Long
    Dim Delay As Long
    Dim LastPitch(&H0 To &HF) As Integer
    Dim OctaveMove(&H0 To &HF) As Integer
    Dim LastDrmNote(&H0 To &HF) As Byte
    Dim CurChn As Byte
    
    MidLen = &H40000
    ReDim MidFile(&H0 To MidLen - 1)
    
    CurPos = PosStart
    MidiEnd = False
    
    CurPos = CurPos + &H4
    Call CopyMemory(MidFile(&H0), &H6468544D, &H4)
    Call CopyMemory(MidFile(&H4), &H6000000, &H4)
    Call CopyMemory(MidFile(&H8), SeqFile(CurPos + &H0), &H2)
    Call CopyMemory(MidFile(&HA), SeqFile(CurPos + &H2), &H2)
    Call CopyMemory(MidFile(&HC), SeqFile(CurPos + &H4), &H2)
    If SeqFile(CurPos + &H3) <> &H1 Then Stop
    Call CopyMemory(MidFile(&HE), &H6B72544D, &H4)
    Call CopyMemory(MidFile(&H12), &HFFFFFFFF, &H4)
    ReDim TempArr(&H0 To &H1)
    Call CopyMemory(TempArr(&H0), SeqFile(CurPos + &H4), &H2)
    TempArr() = ReverseBytes(TempArr())
    Call CopyMemory(MidRes, TempArr(&H0), &H2)
    MidPos = &H16
    CurPos = CurPos + &H6
    
    For TempByt = &H0 To &HF
        LastPitch(TempByt) = &H4000
    Next TempByt
    
    ' Tempo
    MidFile(MidPos + &H0) = &H0
    MidFile(MidPos + &H1) = &HFF
    MidFile(MidPos + &H2) = &H51
    MidFile(MidPos + &H3) = &H3
    Call CopyMemory(MidFile(MidPos + &H4), SeqFile(CurPos), &H3)
    MidPos = MidPos + &H7
    CurPos = CurPos + &H3
    
    If FixMidi Then
        ' Time for Initialiation
        IntroDelay = MidRes / 2 ^ SeqFile(CurPos + &H1) * SeqFile(CurPos + &H0) * 4
        IntroOn = True
        MidFile(MidPos + &H0) = &H0
        MidFile(MidPos + &H1) = &HFF
        MidFile(MidPos + &H2) = &H58
        MidFile(MidPos + &H3) = &H4
        Call CopyMemory(MidFile(MidPos + &H4), SeqFile(CurPos + &H0), &H2)
        MidFile(MidPos + &H6) = &H6 * 2 ^ SeqFile(CurPos + &H1)
        MidFile(MidPos + &H7) = &H8
        MidPos = MidPos + &H8
    End If
    CurPos = CurPos + &H2
    
    Do
        If MidPos + &H20 >= MidLen Then
            MidLen = MidLen + &H40000
            ReDim Preserve MidFile(&H0 To MidLen - 1)
        End If
        
        Delay = &H0
        Do While SeqFile(CurPos) >= &H80
            Delay = Delay * &H80 + (SeqFile(CurPos) And &H7F)
            CurPos = CurPos + &H1
        Loop
        Delay = Delay * &H80 + SeqFile(CurPos)
        CurPos = CurPos + &H1
        
        If FixMidi Then
            ' Finish Initialisation Measure
            If IntroOn Then
                If IntroDelay - Delay <= 0 Then
                    TempLng = IntroDelay
                    TempByt = &H1
                    Do While TempLng >= &H80
                        TempLng = Int(TempLng / &H80)
                        TempByt = TempByt + &H1
                    Loop
                    ReDim TempArr(&H0 To TempByt - 1)
                    
                    TempLng = IntroDelay
                    For TempInt = TempByt - 1 To &H0 Step -1
                        TempArr(TempInt) = TempLng And &H7F Or &H80
                        TempLng = Int(TempLng / &H80)
                    Next TempInt
                    TempArr(TempByt - 1) = TempArr(TempByt - 1) And &H7F
                    Call CopyMemory(MidFile(MidPos), TempArr(&H0), TempByt)
                    MidPos = MidPos + TempByt
                    
                    ' Measure: TempByt / 2 ^ TempInt
                    TempByt = &H4
                    TempInt = &H2
                    MidFile(MidPos + &H0) = &HFF
                    MidFile(MidPos + &H1) = &H58
                    MidFile(MidPos + &H2) = &H4
                    MidFile(MidPos + &H3) = TempByt
                    MidFile(MidPos + &H4) = TempInt
                    MidFile(MidPos + &H5) = &H6 * 2 ^ TempInt
                    MidFile(MidPos + &H6) = &H8
                    MidPos = MidPos + &H7
                    
                    Delay = Delay - IntroDelay
                    IntroOn = False
                    IntroDelay = 0
                Else
                    IntroDelay = IntroDelay - Delay
                End If
            End If
        End If
        
        TempLng = Delay
        TempByt = &H1
        Do While TempLng >= &H80
            TempLng = Int(TempLng / &H80)
            TempByt = TempByt + &H1
        Loop
        ReDim TempArr(&H0 To TempByt - 1)
        
        TempLng = Delay
        For TempInt = TempByt - 1 To &H0 Step -1
            TempArr(TempInt) = TempLng And &H7F Or &H80
            TempLng = Int(TempLng / &H80)
        Next TempInt
        TempArr(TempByt - 1) = TempArr(TempByt - 1) And &H7F
        Call CopyMemory(MidFile(MidPos), TempArr(&H0), TempByt)
        MidPos = MidPos + TempByt
        
        CurChn = SeqFile(CurPos + &H0) And &HF
        Select Case SeqFile(CurPos + &H0)
        Case &H0 To &H7F
            CurChn = LastEvt And &HF
            Select Case LastEvt
            Case &HC0 To &HDF
                Call CopyMemory(MidFile(MidPos), SeqFile(CurPos), &H2)
                MidPos = MidPos + &H1
                CurPos = CurPos + &H1
            Case &H80 To &HEF
                Call CopyMemory(TempLng, SeqFile(CurPos), &H4)
                If TempLng = &H0 Then
                    LastEvt = SeqFile(CurPos + &H0)
                    MidFile(MidPos + &H0) = &HFF
                    MidFile(MidPos + &H1) = &H2F
                    MidFile(MidPos + &H2) = &H0
                    MidPos = MidPos + &H3
                    Exit Do
                End If
                If InsertPB Then
                    If (LastEvt And &HF0) = &H90 Then
                        SeqFile(CurPos + &H0) = SeqFile(CurPos + &H0) + _
                                                OctaveMove(TempByt) * 12
                        If SeqFile(CurPos + &H1) > &H0 Then
                            If LastPitch(TempByt) <> &H4000 Then
                                MidFile(MidPos + &H0) = &HE0 Or CurChn
                                MidFile(MidPos + &H1) = &H0
                                MidFile(MidPos + &H2) = &H40
                                MidFile(MidPos + &H3) = &H0
                                MidFile(MidPos + &H4) = LastEvt
                                MidPos = MidPos + &H5
                                LastPitch(TempByt) = &H4000
                            End If
                        End If
                    ElseIf (LastEvt And &HF0) = &HE0 Then
                        Call CopyMemory(LastPitch(TempByt), SeqFile(CurPos), &H2)
                    End If
                End If
                
                ' Temporary fixes for ToE Dance Music
                If False Then
                If (LastEvt And &HF0) = &H90 And CurChn = &H7 Then
                    TempByt = SeqFile(CurPos + &H0)
                    If SeqFile(CurPos + &H1) > &H0 And LastDrmNote(CurChn) <> TempByt Then
                        MidFile(MidPos + &H0) = &HB0 Or CurChn
                        MidFile(MidPos + &H1) = &H6
                        MidFile(MidPos + &H2) = TempByt + &HF
                        MidFile(MidPos + &H3) = &H0
                        MidFile(MidPos + &H4) = LastEvt
                        MidPos = MidPos + &H5
                        LastDrmNote(CurChn) = TempByt
                    End If
                    SeqFile(CurPos + &H0) = &H31
                End If
                If (LastEvt And &HF0) = &H90 Then
                    If CurChn = &H0 And False Then
                        If SeqFile(CurPos + &H0) = &H1E Then
                            SeqFile(CurPos + &H0) = &H23
                        ElseIf SeqFile(CurPos + &H0) = &H26 Then
                            SeqFile(CurPos + &H0) = &H24
                        End If
                    ElseIf CurChn = &H4 Then
                        SeqFile(CurPos + &H0) = &H28
                    ElseIf CurChn = &H5 Then
                        SeqFile(CurPos + &H0) = &H23
                    End If
                End If
                End If
                Call CopyMemory(MidFile(MidPos), SeqFile(CurPos), &H2)
                MidPos = MidPos + &H2
                CurPos = CurPos + &H2
            Case &HF0
                MsgBox "SysEx ?!"
                Stop
            Case &HFF
                MidFile(MidPos) = LastEvt
                Call CopyMemory(MidFile(MidPos + &H1), SeqFile(CurPos), &H1)
                If SeqFile(CurPos + &H0) = &H51 Then
                    TempInt = &H3
                    Call CopyMemory(MidFile(MidPos + &H3), SeqFile(CurPos + &H1), _
                                    TempInt)
                ElseIf SeqFile(CurPos + &H1) = &H2F Then
                    TempInt = &H0
                    MidiEnd = True
                Else
                    MsgBox "MetaEvent: " & Hex$(SeqFile(CurPos + &H0)) & " ?!"
                    Stop
                End If
                MidFile(MidPos + &H2) = TempInt
                MidPos = MidPos + &H3 + TempInt
                CurPos = CurPos + &H1 + TempInt
            Case Else
                MsgBox "???"
                Stop
            End Select
        Case &HC0 To &HDF
            If FixInsSet And InsData.Head.wInsCount > &H0 Then
                If (SeqFile(CurPos + &H0) And &HF0) = &HC0 Then
                    TempByt = SeqFile(CurPos + &H0) And &HF
                    LastEvt = SeqFile(CurPos + &H1)
                    With InsData.Desc(LastEvt)
                        MidFile(MidPos + &H0) = &HB0 Or TempByt
                        MidFile(MidPos + &H1) = &HB '&H26
                        MidFile(MidPos + &H2) = .bInsVolume
                        MidFile(MidPos + &H3) = &H0
                        MidFile(MidPos + &H4) = &H2A
                        MidFile(MidPos + &H5) = .bInsPanorama
                        MidFile(MidPos + &H6) = &H0
                        MidPos = MidPos + &H7
                    End With
                    If InsData.Desc(LastEvt).bDataCount > &H0 Then
                        With InsData.Data(LastEvt).DataBlk(&H0)
                            MidFile(MidPos + &H0) = &HB0 Or TempByt
                            MidFile(MidPos + &H1) = &H65
                            MidFile(MidPos + &H2) = &H0
                            MidFile(MidPos + &H3) = &H0
                            MidFile(MidPos + &H4) = &H64
                            MidFile(MidPos + &H5) = &H0
                            MidFile(MidPos + &H6) = &H0
                            MidFile(MidPos + &H7) = &H6
                            MidFile(MidPos + &H8) = .bPbDepthMSB
                            MidFile(MidPos + &H9) = &H0
                            MidPos = MidPos + &HA
                            ' useless - MSB and LSB are always the same
                            ' and MIDI doesn't use LSB
                            'MidFile(MidPos + &HA) = &H26
                            'MidFile(MidPos + &HB) = .bPbDepthLSB
                            'MidFile(MidPos + &HC) = &H0
                            'MidPos = MidPos + &HD
                            
                            If .bNoteHigh - .bNoteLow < 8 Then .wUnknown12 = &HFFFF
                            Select Case .wUnknown12
                            Case &HDFEA, &H5FE7 ' Horns
                                SeqFile(CurPos + &H1) = 60
                                OctaveMove(TempByt) = 0
                            Case &H4F88, &H50CB ' Nylon Guitar
                                SeqFile(CurPos + &H1) = 24
                                OctaveMove(TempByt) = 0
                            Case &H5FC3 ' Guitar Freq Noise
                                SeqFile(CurPos + &H1) = 120
                                OctaveMove(TempByt) = 0
                            Case &H5FCD ' Guitar Drum Noise (-> Taiko Drum)
                                SeqFile(CurPos + &H1) = 116
                                OctaveMove(TempByt) = 2
                            Case &H5FCA ' Pan Flute
                                SeqFile(CurPos + &H1) = 7
                                OctaveMove(TempByt) = 1
                            Case &H5FC4, &H5FE9 ' Strings
                                SeqFile(CurPos + &H1) = 48
                                OctaveMove(TempByt) = 0
                            Case &H5FC8, &H5048, &H51C8 ' Strings
                                SeqFile(CurPos + &H1) = 48
                                OctaveMove(TempByt) = 0
                            Case &H5145 ' Flute
                                SeqFile(CurPos + &H1) = 73
                                OctaveMove(TempByt) = 2
                            Case &HCE6B, &HD36B ' Harp
                                SeqFile(CurPos + &H1) = 46
                                OctaveMove(TempByt) = 0
                            Case &HFFFF ' Drums
                                SeqFile(CurPos + &H1) = 127
                                OctaveMove(TempByt) = 0
                            Case &H96DA, &HA5DA, &HCE64 ' Acoustic Piano
                                SeqFile(CurPos + &H1) = 1
                                OctaveMove(TempByt) = 1
                            Case &HCFA4, &H5045 ' Bass
                                SeqFile(CurPos + &H1) = 34
                                OctaveMove(TempByt) = 0
                            Case &H5FE6, &H5FC6 ' Slap-Fretless Bass
                                SeqFile(CurPos + &H1) = 39
                                OctaveMove(TempByt) = 0
                            Case &H5FE4
                                If .bUnknown00 And &H1 Or True Then
                                    ' Church Organ
                                    SeqFile(CurPos + &H1) = 19
                                    OctaveMove(TempByt) = 0
                                Else ' Brass
                                    SeqFile(CurPos + &H1) = 61
                                    OctaveMove(TempByt) = 0
                                End If
                            Case &H5FC5 To &H5FC9, &H52E7 ' Trumpet/Trombone
                                SeqFile(CurPos + &H1) = 57
                                OctaveMove(TempByt) = 0
                            Case &H51C7, &H51C4 ' Brass
                                SeqFile(CurPos + &H1) = 61
                                OctaveMove(TempByt) = 0
                            Case &H4E65 ' Timpani
                                SeqFile(CurPos + &H1) = 47
                                OctaveMove(TempByt) = 0
                            Case &HCFA3 ' Harpsichord
                                SeqFile(CurPos + &H1) = 6
                                OctaveMove(TempByt) = 0
                            Case &H4F8B ' Glockenspiel
                                SeqFile(CurPos + &H1) = 9
                                OctaveMove(TempByt) = 1
                            Case Else   ' Unknown
                                'SeqFile(CurPos + &H1) = 127
                                OctaveMove(TempByt) = 0
                                Debug.Print "Unknown Instrument: " & Format$(LastEvt)
                            End Select
                        End With
                    Else
                        Debug.Print "Illegal Instrument: " & Hex$(LastEvt)
                    End If
                End If
            End If
            
            LastEvt = SeqFile(CurPos + &H0)
            Call CopyMemory(MidFile(MidPos), SeqFile(CurPos), &H2)
            MidPos = MidPos + &H2
            CurPos = CurPos + &H2
        Case &H80 To &HEF
            LastEvt = SeqFile(CurPos + &H0)
            If InsertPB Then
                TempByt = SeqFile(CurPos) And &HF
                If (SeqFile(CurPos + &H0) And &HF0) = &H90 Then
                    SeqFile(CurPos + &H1) = SeqFile(CurPos + &H1) + _
                                            OctaveMove(TempByt) * 12
                    If SeqFile(CurPos + &H2) > &H0 Then
                        If LastPitch(TempByt) <> &H4000 Then
                            MidFile(MidPos + &H0) = &HE0 Or TempByt
                            MidFile(MidPos + &H1) = &H0
                            MidFile(MidPos + &H2) = &H40
                            MidFile(MidPos + &H3) = &H0
                            MidPos = MidPos + &H4
                            LastPitch(TempByt) = &H4000
                        End If
                    End If
                ElseIf (SeqFile(CurPos + &H0) And &HF0) = &HE0 Then
                    Call CopyMemory(LastPitch(TempByt), SeqFile(CurPos + &H1), &H2)
                End If
            End If
            ' Temporary fixes for ToE Dance Music
            If False Then
            If (LastEvt And &HF0) = &H90 And CurChn = &H7 Then
                TempByt = SeqFile(CurPos + &H1)
                If SeqFile(CurPos + &H2) > &H0 And LastDrmNote(CurChn) <> TempByt Then
                    MidFile(MidPos + &H0) = &HB0 Or CurChn
                    MidFile(MidPos + &H1) = &H6
                    MidFile(MidPos + &H2) = TempByt + &HF
                    MidFile(MidPos + &H3) = &H0
                    LastDrmNote(CurChn) = TempByt
                    MidPos = MidPos + &H4
                End If
                SeqFile(CurPos + &H1) = &H31
            End If
            If (LastEvt And &HF0) = &H90 Then
                If CurChn = &H0 And False Then
                    If SeqFile(CurPos + &H1) = &H1E Then
                        SeqFile(CurPos + &H1) = &H23
                    ElseIf SeqFile(CurPos + &H1) = &H26 Then
                        SeqFile(CurPos + &H1) = &H24
                    End If
                ElseIf CurChn = &H4 Then
                    SeqFile(CurPos + &H1) = &H28
                ElseIf CurChn = &H5 Then
                    SeqFile(CurPos + &H1) = &H23
                End If
            End If
            End If
            
            Call CopyMemory(MidFile(MidPos), SeqFile(CurPos), &H3)
            MidPos = MidPos + &H3
            CurPos = CurPos + &H3
        Case &HF0
            MsgBox "SysEx ?!"
            Stop
            LastEvt = SeqFile(CurPos + &H0)
        Case &HFF
            LastEvt = SeqFile(CurPos + &H0)
            Call CopyMemory(MidFile(MidPos), SeqFile(CurPos), &H2)
            If SeqFile(CurPos + &H1) = &H51 Then
                TempInt = &H3
                Call CopyMemory(MidFile(MidPos + &H3), SeqFile(CurPos + &H2), TempInt)
            ElseIf SeqFile(CurPos + &H1) = &H2F Then
                TempInt = &H0
                MidiEnd = True
            Else
                MsgBox "MetaEvent: " & Hex$(SeqFile(CurPos + &H1)) & " ?!"
                Stop
            End If
            MidFile(MidPos + &H2) = TempInt
            MidPos = MidPos + &H3 + TempInt
            CurPos = CurPos + &H2 + TempInt
        Case Else
            MsgBox "???"
            Stop
        End Select
    Loop Until MidiEnd
    MidLen = MidPos
    
    TempLng = MidLen - &H16
    ReDim TempArr(&H0 To &H3)
    Call CopyMemory(TempArr(&H0), TempLng, &H4)
    TempArr() = ReverseBytes(TempArr())
    Call CopyMemory(MidFile(&H12), TempArr(&H0), &H4)
    ReDim Preserve MidFile(&H0 To MidLen - 1)
    
    RetData() = MidFile()
    PosStart = CurPos
    Seq2MidConversion = MidLen

End Function

Private Function Seq2InsConversion(ByVal PosStart As Long, ByRef RetData() As Byte, _
                                    ByRef RetIns As INSTRUMENT_DATA) As Long

    Dim CurPos As Long
    Dim InsLen As Long
    Dim CurIns As Byte
    Dim CurLin As Byte
    Dim InsNo As Byte
    Dim TempInt As Integer
    
    CurPos = PosStart
    
    ReDim RetIns.Desc(&H0 To INS_MAX_INSTRUMENTS - 1)
    ReDim RetIns.Data(&H0 To INS_MAX_INSTRUMENTS - 1)
    
    With RetIns.Head
        Call CopyMemory(.fccSignature, SeqFile(CurPos + &H0), &H4)
        Call CopyMemory(.dwUnknown04, SeqFile(CurPos + &H4), &H4)
        Call CopyMemory(.dwUnknown08, SeqFile(CurPos + &H8), &H4)
        Call CopyMemory(.dwInsSize, SeqFile(CurPos + &HC), &H4)
        Call CopyMemory(.wUnknown10, SeqFile(CurPos + &H10), &H2)
        Call CopyMemory(.wInsCount, SeqFile(CurPos + &H12), &H2)
        Call CopyMemory(.wInsLineUsed, SeqFile(CurPos + &H14), &H2)
        Call CopyMemory(.wInsIDCount, SeqFile(CurPos + &H16), &H2)
        .bMasterVol = SeqFile(CurPos + &H18)
        .bMasterPan = SeqFile(CurPos + &H19)
        .bUnknown1A = SeqFile(CurPos + &H1A)
        .bUnknown1B = SeqFile(CurPos + &H1B)
        Call CopyMemory(.dwPadding, SeqFile(CurPos + &H1C), &H4)
        CurPos = CurPos + &H20
    End With
    
    For CurIns = &H0 To INS_MAX_INSTRUMENTS - 1
        ReDim RetIns.Data(CurIns).DataBlk(&H0 To INS_DATA_BLK_LINES - 1)
        
        With RetIns.Desc(CurIns)
            .bDataCount = SeqFile(CurPos + &H0)
            .bInsVolume = SeqFile(CurPos + &H1)
            Call CopyMemory(.wUnknown02, SeqFile(CurPos + &H2), &H2)
            .bInsPanorama = SeqFile(CurPos + &H4)
            .bUnknown05 = SeqFile(CurPos + &H5)
            .bUnknown06 = SeqFile(CurPos + &H6)
            .bUnknown07 = SeqFile(CurPos + &H7)
            Call CopyMemory(.dwPadding08, SeqFile(CurPos + &H8), &H4)
            Call CopyMemory(.dwPadding0C, SeqFile(CurPos + &HC), &H4)
            CurPos = CurPos + &H10
        End With
    Next CurIns
    
    For CurIns = &H0 To RetIns.Head.wInsCount - 1
        For CurLin = &H0 To INS_DATA_BLK_LINES - 1
            Call CopyMemory(TempInt, SeqFile(CurPos + &H16), &H2)
            If TempInt <> &H0 Then
                InsNo = SeqFile(CurPos + &H14)
                With RetIns.Data(InsNo).DataBlk(CurLin)
                    .bUnknown00 = SeqFile(CurPos + &H0)
                    .bUnknown01 = SeqFile(CurPos + &H1)
                    .bToneVol = SeqFile(CurPos + &H2)
                    .bTonePan = SeqFile(CurPos + &H3)
                    .bUnknown04 = SeqFile(CurPos + &H4)
                    .bUnknown05 = SeqFile(CurPos + &H5)
                    .bNoteLow = SeqFile(CurPos + &H6)
                    .bNoteHigh = SeqFile(CurPos + &H7)
                    If .bNoteLow > .bNoteHigh Then
                        .bNoteLow = &H0
                    End If
                    Call CopyMemory(.dwUnknown08, SeqFile(CurPos + &H8), &H4)
                    .bPbDepthMSB = SeqFile(CurPos + &HC)
                    .bPbDepthLSB = SeqFile(CurPos + &HD)
                    Call CopyMemory(.wUnknown0E, SeqFile(CurPos + &HE), &H2)
                    Call CopyMemory(.wUnknown10, SeqFile(CurPos + &H10), &H2)
                    Call CopyMemory(.wUnknown12, SeqFile(CurPos + &H12), &H2)
                    Call CopyMemory(.wInsNumber, SeqFile(CurPos + &H14), &H2)
                    Call CopyMemory(.wInsID, SeqFile(CurPos + &H16), &H2)
                    Call CopyMemory(.wUnknown18, SeqFile(CurPos + &H18), &H2)
                    Call CopyMemory(.wUnknown1A, SeqFile(CurPos + &H1A), &H2)
                    Call CopyMemory(.wUnknown1C, SeqFile(CurPos + &H1C), &H2)
                    Call CopyMemory(.wUnknown1E, SeqFile(CurPos + &H1E), &H2)
                    CurPos = CurPos + &H20
                End With
            Else
                CurPos = CurPos + &H20
            End If
        Next CurLin
    Next CurIns
    
    InsLen = CurPos - PosStart
    ReDim RetData(&H0 To InsLen - 1)
    Call CopyMemory(RetData(&H0), SeqFile(PosStart), InsLen)
    
    Seq2InsConversion = InsLen

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
