Attribute VB_Name = "MainModule"
' Monkey Island 1 Midi Converter
Private Declare Sub CopyMemory Lib "kernel32" Alias "RtlMoveMemory" _
    (ByRef hpvDest As Any, ByRef hpvSource As Any, ByVal cbCopy As Long)

Sub Main()

    Dim InFile As String
    Dim OutFile As String
    Dim FilePath As String
    
    FilePath = "E:\monkey1\Midi-Rip\"
    InFile = "Rip_07.bin"
    OutFile = Left$(InFile, InStrRev(InFile, ".")) & "mid"
    
    Call ConvertBin2Mid(FilePath & InFile, FilePath & OutFile)

End Sub

Private Sub ConvertBin2Mid(FileInput As String, FileOutput As String)

    Dim BinLen As Long
    Dim BinData() As Byte
    Dim MidLen As Long
    Dim MidData() As Byte
    Dim CurIPos As Long
    Dim CurOPos As Long
    Dim TempLng As Long
    Dim TempInt As Long
    Dim ArrSize As Long
    Dim TempArr() As Byte
    Dim Delay As Long
    Dim TempStr As String
    
    If Dir(FileInput, vbHidden) = "" Then
        Debug.Print "Error! File not found!"
        Exit Sub
    End If
    
    Open FileInput For Binary Access Read As #1
        BinLen = LOF(1)
        ReDim BinData(&H0 To BinLen - 1)
        Get #1, 1, BinData()
    Close #1
    
    MidLen = BinLen + &H40
    ReDim MidData(&H0 To MidLen - 1)
    
    Call CopyMemory(BinLen, BinData(&H8), &H4)
    BinLen = BinLen + &HC
    
    TempLng = Str2Long("MThd")
    Call CopyMemory(MidData(&H0), TempLng, &H4)
    TempLng = IntelMotorolaCnvLong(&H6)
    Call CopyMemory(MidData(&H4), TempLng, &H4)
    TempInt = IntelMotorolaCnvShort(&H0)
    Call CopyMemory(MidData(&H8), TempInt, &H2)
    TempInt = IntelMotorolaCnvShort(&H1)
    Call CopyMemory(MidData(&HA), TempInt, &H2)
    TempInt = IntelMotorolaCnvShort(&H78)
    Call CopyMemory(MidData(&HC), TempInt, &H2)
    TempLng = Str2Long("MTrk")
    Call CopyMemory(MidData(&HE), TempLng, &H4)
    TempLng = IntelMotorolaCnvLong(&HFFFFFFFF)
    Call CopyMemory(MidData(&H12), TempLng, &H4)
    
    CurOPos = &H16
    MidData(CurOPos + &H0) = &H0
    MidData(CurOPos + &H1) = &HFF
    MidData(CurOPos + &H2) = &H51
    MidData(CurOPos + &H3) = &H3
    MidData(CurOPos + &H4) = &H7
    MidData(CurOPos + &H5) = &HA1
    MidData(CurOPos + &H6) = &H20
    CurOPos = CurOPos + &H7
    
    CurIPos = &H10
    Do While CurIPos < BinLen
        Select Case BinData(CurIPos + &H0)
        Case &HF0
            Delay = Delay + BinData(CurIPos + &H1)
            CurIPos = CurIPos + &H2
        Case &HA0
            CurIPos = CurIPos + &H1
        Case &H80 To &HEF, &HFF
            ' Write Delay
            TempLng = Delay
            ArrSize = &H1
            Do While TempLng >= &H80
                TempLng = Int(TempLng / &H80)
                ArrSize = ArrSize + &H1
            Loop
            
            ReDim TempArr(&H0 To ArrSize - 1)
            TempLng = Delay
            TempInt = ArrSize - &H1
            TempArr(TempInt) = (TempLng And &H7F) Or &H0
            Do While TempInt > &H0
                TempInt = TempInt - &H1
                TempLng = Int(TempLng / &H80)
                TempArr(TempInt) = (TempLng And &H7F) Or &H80
            Loop
            
            Call CopyMemory(MidData(CurOPos), TempArr(&H0), ArrSize)
            CurOPos = CurOPos + ArrSize
            Delay = &H0
            
            ' Write Event
            Select Case BinData(CurIPos + &H0) And &HF0
            Case &H80, &H90, &HB0
                MidData(CurOPos + &H0) = BinData(CurIPos + &H0)
                MidData(CurOPos + &H1) = BinData(CurIPos + &H1)
                MidData(CurOPos + &H2) = BinData(CurIPos + &H2)
                CurIPos = CurIPos + &H3
                CurOPos = CurOPos + &H3
            Case &HC0
                MidData(CurOPos + &H0) = BinData(CurIPos + &H0)
                MidData(CurOPos + &H1) = BinData(CurIPos + &H1)
                CurIPos = CurIPos + &H2
                CurOPos = CurOPos + &H2
            Case &HF0
                TempStr = "Loop End"
                TempLng = Len(TempStr) And &H7F
                MidData(CurOPos + &H0) = &HFF
                MidData(CurOPos + &H1) = &H6
                MidData(CurOPos + &H2) = TempLng
                TempArr() = StrConv(TempStr, vbFromUnicode)
                Call CopyMemory(MidData(CurOPos + &H3), TempArr(&H0), TempLng)
                CurIPos = CurIPos + &H1
                CurOPos = CurOPos + &H3 + TempLng
            Case Else
                Debug.Print Hex$(BinData(CurIPos + &H0))
                Stop
            End Select
        Case Else
            Debug.Print Hex$(BinData(CurIPos + &H0))
            Stop
        End Select
    Loop
    
    MidData(CurOPos + &H0) = &H0
    MidData(CurOPos + &H1) = &HFF
    MidData(CurOPos + &H2) = &H2F
    MidData(CurOPos + &H3) = &H0
    CurOPos = CurOPos + &H4
    TempLng = CurOPos - &H16
    TempLng = IntelMotorolaCnvLong(TempLng)
    Call CopyMemory(MidData(&H12), TempLng, &H4)
    ReDim Preserve MidData(&H0 To CurOPos - 1)
    
    Open FileOutput For Output As #2
    Close #2
    Open FileOutput For Binary Access Write As #2
        Put #2, 1, MidData()
    Close #2

End Sub

Private Function Str2Long(FCC As String) As Long

    Dim TempArr() As Byte
    Dim RetVal As Long
    
    TempArr() = StrConv(FCC, vbFromUnicode)
    Call CopyMemory(RetVal, TempArr(&H0), &H4)
    
    Str2Long = RetVal

End Function

Private Function IntelMotorolaCnvLong(Value As Long) As Long

    Dim TempSrc(&H0 To &H3) As Byte
    Dim TempDst(&H0 To &H3) As Byte
    Dim RetVal As Long
    
    Call CopyMemory(TempSrc(&H0), Value, &H4)
    TempDst(&H0) = TempSrc(&H3)
    TempDst(&H1) = TempSrc(&H2)
    TempDst(&H2) = TempSrc(&H1)
    TempDst(&H3) = TempSrc(&H0)
    Call CopyMemory(RetVal, TempDst(&H0), &H4)
    
    IntelMotorolaCnvLong = RetVal

End Function

Private Function IntelMotorolaCnvShort(Value As Integer) As Integer

    Dim TempSrc(&H0 To &H1) As Byte
    Dim TempDst(&H0 To &H1) As Byte
    Dim RetVal As Long
    
    Call CopyMemory(TempSrc(&H0), Value, &H2)
    TempDst(&H0) = TempSrc(&H1)
    TempDst(&H1) = TempSrc(&H0)
    Call CopyMemory(RetVal, TempDst(&H0), &H2)
    
    IntelMotorolaCnvShort = RetVal

End Function
