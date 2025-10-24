//
//  GaussianHeader.swift
//  SwiftGaussian
//
//  Created by Avinash Chidambaram on 23/10/25.
//

import Foundation

struct PackedGaussiansHeader {
    var magic: UInt32
    var version: UInt32
    var numPoints: UInt32
    var shDegree: UInt8
    var fractionalBits: UInt8
    var flags: UInt8
    var reserved: UInt8
}
