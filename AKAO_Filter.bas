Attribute VB_Name = "Module1"
Option Explicit

Sub Main()

    Dim FilePath As String
    Dim FileCount As Long
    Dim FileList() As String
    Dim FileStr As String
    Dim CurFile As Long
    Dim FileSize As Long
    Dim FileData() As Byte
    Dim NewSize As Long
    Dim NewData() As Byte
    Dim CurPos As Long
    Dim NewPos As Long
    
    FilePath = "D:\Images\CD\PSFCnv\CC\"
    FileCount = &H0
    FileStr = Dir(FilePath & "*.psf")
    Do Until FileStr = ""
        ReDim Preserve FileList(&H0 To FileCount)
        FileList(FileCount) = FileStr
        FileCount = FileCount + &H1
        
        FileStr = Dir()
    Loop
    
    For CurFile = &H0 To FileCount - 1
        Open FilePath & FileList(CurFile) For Binary Access Read As #1
            FileSize = LOF(1)
            ReDim FileData(&H0 To FileSize - 1)
            Get #1, 1, FileData()
        Close #1
        
        CurPos = &H0
        NewSize = &H0
        Do While CurPos < FileSize
            If FileData(CurPos) = &H41 Then
                If FileData(CurPos + &H1) = &H4B And FileData(CurPos + &H2) = &H41 And _
                    FileData(CurPos + &H3) = &H4F Then
                    NewSize = &H1
                    Exit Do
                End If
            End If
            CurPos = CurPos + &H1
        Loop
        
        If NewSize Then
            NewSize = FileData(CurPos + &H6) Or FileData(CurPos + &H7) * &H100&
            NewSize = NewSize + &H10
            
            ReDim NewData(&H0 To NewSize - 1)
            For NewPos = &H0 To NewSize - 1
                NewData(NewPos) = FileData(CurPos + NewPos)
            Next NewPos
            
            FileStr = Left$(FileList(CurFile), InStrRev(FileList(CurFile), ".")) & _
                        "ako"
            Debug.Print FileStr
            Open FilePath & FileStr For Binary Access Write As #2
                Put #2, 1, NewData()
            Close #2
        Else
            Debug.Print FileList(CurFile) & " - Null-File found!"
        End If
    Next CurFile

End Sub
