//
//  ViewController.swift
//  SwiftGaussian
//
//  Created by Avinash Chidambaram on 23/10/25.
//

import UIKit
import MetalKit
import GaussianSwift
import zlib


class ViewController: UIViewController, MTKViewDelegate {
    
    var device: MTLDevice!
    var metalView: MTKView!
    var commandQueue: MTLCommandQueue!
    var lastTouchLocation: CGPoint = .zero

    override func viewDidLoad() {
        super.viewDidLoad()
        guard let device = MTLCreateSystemDefaultDevice() else {
            print("Metal not on device")
            return
        }
        self.device = device
        
        metalView = MTKView(frame: view.bounds, device: device)
        metalView.delegate = self
        metalView.colorPixelFormat = .bgra8Unorm
        metalView.clearColor = MTLClearColorMake(1, 0, 0, 1)
        metalView.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(metalView)
        
        NSLayoutConstraint.activate([
            metalView.leadingAnchor.constraint(equalTo: view.leadingAnchor),
            metalView.trailingAnchor.constraint(equalTo: view.trailingAnchor),
            metalView.topAnchor.constraint(equalTo: view.topAnchor),
            metalView.bottomAnchor.constraint(equalTo: view.bottomAnchor)
        ])
        
        commandQueue = device.makeCommandQueue()
        initializeSokol()
        setupGestures()
    }
    
    func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {
        mark_swapchain_needs_update_ios()
    }
    
    func draw(in view: MTKView) {
        self.renderWithSokol()
    }
    
    
    
    func loadSPZFile(){
        
        print("Starting SPZ file processing");
        
        guard let asset = NSDataAsset(name: "furry") else {
            print("no asset exist")
            return
        }
        
        let compressedData = asset.data
        let fileSize = compressedData.count
        
        print("the size of file is \(fileSize) bytes")
        
        if fileSize == 0 {
            print("lol empty file")
            return
        }
        
        
        print("decompressing spz now")
        
        var strm = z_stream()
        
        var ret = inflateInit2_(&strm, 16 + MAX_WBITS, ZLIB_VERSION, Int32(MemoryLayout<z_stream>.size))
        guard ret == Z_OK else {
            print("ERROR: Failed to init zlib inflate")
            return
        }
        
        let inputPointer = (compressedData as NSData).bytes.bindMemory(to: Bytef.self, capacity: fileSize)
        
        strm.avail_in = uInt(fileSize)
        strm.next_in = UnsafeMutablePointer<Bytef>(mutating: inputPointer)
        
        var outSize = fileSize * 10
        let outBuffer = UnsafeMutablePointer<UInt8>.allocate(capacity: outSize)
        strm.avail_out = uInt(outSize)
        strm.next_out = outBuffer
        ret = inflate(&strm, Z_FINISH)
        if ret != Z_STREAM_END {
            print("ERROR: zlib decompression failed: \(ret)")
            outBuffer.deallocate()
            inflateEnd(&strm)
            return
        }
        
        let decompressedSize = outSize - Int(strm.avail_out)
        inflateEnd(&strm)
        print("Decompression success: \(decompressedSize) bytes")
        
        let result = parse_spz_data(outBuffer, decompressedSize)
        if result != 0 {
            print("ERROR: Failed to parse SPZ data")
        }
        
        outBuffer.deallocate()
        print("SPZ file processing completed!")
        
    }
    
    func initializeSokol(){
        let devicePointer = Unmanaged.passUnretained(self.device).toOpaque()
        initGpuWithMetalDevice(devicePointer)
        let currentSize = metalView.drawableSize
        init_renderer(Int32(currentSize.width), Int32(currentSize.height))
        
        self.loadSPZFile()
    }
    
    func updateSwapchainIfNeeded(){
        let currentSize = self.metalView.drawableSize
        update_swapchain_ios(Int32(currentSize.width), Int32(currentSize.height))
    }
    
    
    func renderWithSokol(){
        if (!is_renderer_initialized()){
            return
        }
        
        guard let drawable = self.metalView.currentDrawable else {
            print("no drawable noooo")
            return
        }
        
        self.updateSwapchainIfNeeded()
        
        let drawablePointer = Unmanaged.passUnretained(drawable).toOpaque()
        
        let nullPointer: UnsafeMutableRawPointer? = nil
        
        render_frame_ios(drawablePointer, nullPointer)
        
        
    }
    
    func cleanupSokol() {
        if is_renderer_initialized() {
            cleanup_renderer()
        }
    }
    
    deinit {
//        DispatchQueue.main.async {
//            self.cleanupSokol()
//        }
    }
    
    
    
    
    
    
    
    func setupGestures() {
            let pan = UIPanGestureRecognizer(target: self, action: #selector(handlePan(_:)))
            let pinch = UIPinchGestureRecognizer(target: self, action: #selector(handlePinch(_:)))
            metalView.addGestureRecognizer(pan)
            metalView.addGestureRecognizer(pinch)
        }
        
        @objc func handlePan(_ gesture: UIPanGestureRecognizer) {
            let location = gesture.location(in: metalView)
            switch gesture.state {
            case .began:
                handle_touch_down(Float(location.x), Float(location.y))
                lastTouchLocation = location
            case .changed:
                handle_input(Float(location.x), Float(location.y))
                lastTouchLocation = location
            case .ended, .cancelled:
                handle_touch_up()
            default:
                break
            }
        }
        
        @objc func handlePinch(_ gesture: UIPinchGestureRecognizer) {
            if gesture.state == .changed {
                handle_pinch(Float(gesture.scale))
                gesture.scale = 1.0
            }
        }
    


}

