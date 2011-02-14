Attribute VB_Name = "Module1"
Option Explicit

Private Const FILE_PATH As String = "E:\LEM_DOS\3DLem\SOUND\GM\"

Sub Main()

    Dim FileName As String
    Dim FileCount As Long
    Dim FileList() As String
    Dim CurFile As Long
    
    FileCount = &H0
    FileName = Dir(FILE_PATH & "*.SND")
    Do Until FileName = ""
        ReDim Preserve FileList(&H0 To FileCount)
        FileList(FileCount) = FileName
        FileCount = FileCount + &H1
        
        FileName = Dir()
    Loop
    
    For CurFile = &H0 To FileCount - 1
        Call ConvertL3DtoMID(FileList(CurFile))
    Next CurFile

End Sub

Private Sub ConvertL3DtoMID(ByVal FileName As String)

    Dim InSize As Long
    Dim InData() As Byte
    Dim OutSize As Long
    Dim OutData() As Byte
    Dim InPos As Long
    Dim OutPos As Long
    
    Dim TrkCount As Integer
    Dim TrkTOC() As Long
    Dim Tempo As Byte
    
    Dim TempLng As Long
    Dim MTrkBase As Long
    Dim CurTrk As Integer
    Dim SelChn As Byte
    Dim LastNote As Byte
    
    Dim MidName As String
    
    If FileLen(FILE_PATH & FileName) = 0 Then Exit Sub
    Open FILE_PATH & FileName For Binary Access Read As #1
        InSize = LOF(1)
        ReDim InData(&H0 To InSize - 1)
        Get #1, 1, InData()
    Close #1
    
    OutSize = InSize * &H2
    ReDim OutData(&H0 To OutSize - 1)
    OutData(&H0) = &H4D
    OutData(&H1) = &H54
    OutData(&H2) = &H68
    OutData(&H3) = &H64
    OutData(&H4) = &H0
    OutData(&H5) = &H0
    OutData(&H6) = &H0
    OutData(&H7) = &H6
    OutData(&H8) = &H0
    OutData(&H9) = &H1
    
    ' Jump to Footer (pointer at 0001h points to "footer-pointer")
    InPos = InData(&H2) * &H100 Or InData(&H1)
    InPos = InData(InPos + &H1) * &H100 Or InData(InPos + &H0)
    
    ' Read Footer
    OutData(&HC) = &H0
    OutData(&HD) = InData(InPos + &H0)  ' Ticks per Quarter
    Tempo = InData(InPos + &H1)
    TrkCount = InData(InPos + &H2)
    OutData(&HA) = &H0
    OutData(&HB) = TrkCount
    InPos = InPos + &H3
    
    ReDim TrkTOC(&H0 To TrkCount - 1)
    For CurTrk = &H0 To TrkCount - 1
        TrkTOC(CurTrk) = InData(InPos + &H1) * &H100 Or InData(InPos + &H0)
        InPos = InPos + &H2
    Next CurTrk
    
    OutPos = &HE
    For CurTrk = &H0 To TrkCount - 1
        OutData(OutPos + &H0) = &H4D
        OutData(OutPos + &H1) = &H54
        OutData(OutPos + &H2) = &H72
        OutData(OutPos + &H3) = &H6B
        OutPos = OutPos + &H8
        MTrkBase = OutPos
        
        If CurTrk = &H0 Then
            ' Write Tempo
            TempLng = 1000000 / (Tempo / 60)
            OutData(OutPos) = &H0   ' Delay
            OutPos = OutPos + &H1
            OutData(OutPos + &H0) = &HFF
            OutData(OutPos + &H1) = &H51
            OutData(OutPos + &H2) = &H3
            OutData(OutPos + &H3) = Int((TempLng And &HFF0000) / &H10000)
            OutData(OutPos + &H4) = Int((TempLng And &HFF00&) / &H100)
            OutData(OutPos + &H5) = TempLng And &HFF
            OutPos = OutPos + &H6
        End If
        
        InPos = TrkTOC(CurTrk)
        Do
            Do While InData(InPos) And &H80
                OutData(OutPos) = InData(InPos)
                InPos = InPos + &H1
                OutPos = OutPos + &H1
            Loop
            OutData(OutPos) = InData(InPos)
            InPos = InPos + &H1
            OutPos = OutPos + &H1
            
            If InData(InPos + &H0) < &H80 Then
                OutData(OutPos + &H0) = &H90 Or SelChn
                OutData(OutPos + &H1) = InData(InPos + &H0)
                OutData(OutPos + &H2) = InData(InPos + &H1)
                LastNote = InData(InPos + &H0)
                InPos = InPos + &H2
                OutPos = OutPos + &H3
            Else
                Select Case InData(InPos + &H0)
                Case &H80 To &H8F   ' Select Channel
                    SelChn = InData(InPos + &H0) And &HF
                    OutData(OutPos + &H0) = &HFF
                    OutData(OutPos + &H1) = &H20
                    OutData(OutPos + &H2) = &H1
                    OutData(OutPos + &H3) = SelChn
                    InPos = InPos + &H1
                    OutPos = OutPos + &H4
                Case &H97   ' Drum Select (only present in Sound Blaster files)
                    OutData(OutPos + &H0) = &HB0 Or SelChn
                    OutData(OutPos + &H1) = &H67
                    OutData(OutPos + &H2) = InData(InPos + &H1)
                    InPos = InPos + &H2
                    OutPos = OutPos + &H3
                Case &H9C   ' Loop Start
                    OutData(OutPos + &H0) = &HB0 Or SelChn
                    OutData(OutPos + &H1) = &H6F
                    OutData(OutPos + &H2) = &H0
                    InPos = InPos + &H1
                    OutPos = OutPos + &H3
                Case &H98   ' Loop Back
                    OutData(OutPos + &H0) = &HB0 Or SelChn
                    OutData(OutPos + &H1) = &H6F
                    OutData(OutPos + &H2) = &H1
                    InPos = InPos + &H1
                    OutPos = OutPos + &H3
                    OutData(OutPos) = &H0   ' Add a Delay
                    OutPos = OutPos + &H1
                    Exit Do
                Case &H91   ' Track End
                    InPos = InPos + &H1
                    Exit Do
                Case &H99   ' Turn Last Note Off
                    OutData(OutPos + &H0) = &H90 Or SelChn
                    OutData(OutPos + &H1) = LastNote
                    OutData(OutPos + &H2) = &H0
                    InPos = InPos + &H1
                    OutPos = OutPos + &H3
                Case &H90   ' Turn Note Off
                    OutData(OutPos + &H0) = &H90 Or SelChn
                    OutData(OutPos + &H1) = InData(InPos + &H1)
                    OutData(OutPos + &H2) = &H0
                    InPos = InPos + &H2
                    OutPos = OutPos + &H3
                Case &H92   ' Instrument Change
                    OutData(OutPos + &H0) = &HC0 Or SelChn
                    OutData(OutPos + &H1) = InData(InPos + &H1)
                    InPos = InPos + &H2
                    OutPos = OutPos + &H2
                Case &H95   ' Pitch Bend
                    OutData(OutPos + &H0) = &HE0 Or SelChn
                    OutData(OutPos + &H1) = &H0
                    OutData(OutPos + &H2) = InData(InPos + &H1)
                    InPos = InPos + &H2
                    OutPos = OutPos + &H3
                Case &H96   ' Volume Controller
                    OutData(OutPos + &H0) = &HB0 Or SelChn
                    OutData(OutPos + &H1) = &H7
                    OutData(OutPos + &H2) = InData(InPos + &H1)
                    InPos = InPos + &H2
                    OutPos = OutPos + &H3
                Case &H9B   ' Panorama Controller
                    OutData(OutPos + &H0) = &HB0 Or SelChn
                    OutData(OutPos + &H1) = &HA
                    OutData(OutPos + &H2) = InData(InPos + &H1)
                    InPos = InPos + &H2
                    OutPos = OutPos + &H3
                Case &H9D   ' Controller (any)
                    OutData(OutPos + &H0) = &HB0 Or SelChn
                    OutData(OutPos + &H1) = InData(InPos + &H1)
                    OutData(OutPos + &H2) = InData(InPos + &H2)
                    InPos = InPos + &H3
                    OutPos = OutPos + &H3
                Case Else
                    Debug.Print Hex$(InData(InPos + &H0))
                    Stop
                End Select
            End If
        Loop
        OutData(OutPos + &H0) = &HFF
        OutData(OutPos + &H1) = &H2F
        OutData(OutPos + &H2) = &H0
        OutPos = OutPos + &H3
        
        TempLng = OutPos - MTrkBase
        OutData(MTrkBase - &H4) = Int((TempLng And &H7F000000) / &H1000000)
        OutData(MTrkBase - &H3) = Int((TempLng And &HFF0000) / &H10000)
        OutData(MTrkBase - &H2) = Int((TempLng And &HFF00&) / &H100)
        OutData(MTrkBase - &H1) = TempLng And &HFF
    Next CurTrk
    
    OutSize = OutPos
    ReDim Preserve OutData(&H0 To OutSize - 1)
    
    TempLng = InStrRev(FileName, ".")
    If TempLng = 0 Then TempLng = Len(FileName) + 1
    MidName = Left$(FileName, TempLng - 1) & ".MID"
    Open FILE_PATH & MidName For Binary Access Write As #2
        Put #2, 1, OutData()
    Close #2

End Sub
