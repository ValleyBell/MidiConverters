Attribute VB_Name = "Module1"
Option Explicit

Sub Main()

    Const PATH = "E:\LEM_DOS\Lemm3\AUDIO\SNDBLSTR\TUNE003."
    
    Dim SrcPos As Long
    Dim DstPos As Long
    Dim TrkCount As Integer
    Dim CurTrk As Integer
    Dim TempLng As Long
    Dim TempSht As Integer
    Dim TempByt As Byte
    Dim DlyArr(&H0 To &HF) As Byte
    
    Open PATH & "HMI" For Binary Access Read As #1
    Open PATH & "MID" For Binary Access Write As #2
    
    Get #1, 1 + &H1A, TrkCount
    Put #2, 1 + &H0, "MThd"
    Put #2, , CLng(&H6000000)
    Put #2, , CInt(&H100)
    Put #2, , CByte(TrkCount \ &H100)
    Put #2, , CByte(TrkCount And &HFF)
    Put #2, , CInt(&H2300)
    
    Seek #1, 1 + &H186
    For CurTrk = &H0 To TrkCount - 1
        Get #1, , TempSht
        Put #2, , "MTrk"
        Get #1, , TempSht
        TempSht = TempSht - &H6
        Put #2, , CByte(&H0)
        Put #2, , CByte(&H0)
        Put #2, , CByte((TempSht And &HFF00&) \ &H100&)
        Put #2, , CByte((TempSht And &HFF&) \ &H1&)
        Get #1, , TempSht
        Debug.Print "Track " & CurTrk & ": " & Hex$(TempSht)
        
        Do
            TempSht = &H0
            Get #1, , TempByt
            Do Until TempByt And &H80
                DlyArr(TempSht) = TempByt
                TempSht = TempSht + &H1
                Get #1, , TempByt
            Loop
            DlyArr(TempSht) = TempByt And &H7F
            
            Do While TempSht > &H0
                TempByt = &H80 Or DlyArr(TempSht)
                Put #2, , TempByt
                TempSht = TempSht - &H1
            Loop
            Put #2, , DlyArr(TempSht)
            
            Get #1, , TempByt
            Select Case TempByt And &HF0
            Case &H80, &H90, &HA0, &HB0, &HE0
                Get #1, , TempSht
                If (TempByt And &HF0) = &HB0 And TempSht = &H6C Then
                    TempByt = &HFF
                    TempSht = &H0
                End If
                Put #2, , TempByt
                Put #2, , TempSht
            Case &HC0, &HD0
                Put #2, , TempByt
                Get #1, , TempByt
                Put #2, , TempByt
            Case &HF0
                If TempByt <> &HFF Then Stop
                Put #2, , TempByt   ' FF
                Get #1, , TempByt   ' 2F
                Put #2, , TempByt
                If TempByt <> &H2F Then Stop
                Get #1, , TempByt   ' 00
                Put #2, , TempByt
                Exit Do
            End Select
        Loop
    Next CurTrk
    
    Close #2
    Close #1

End Sub

