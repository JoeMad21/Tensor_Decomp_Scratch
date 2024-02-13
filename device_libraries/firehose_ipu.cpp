#include "firehose_ipu.hpp"

enum Progs {
    STREAM_INPUTS,
    ALIGN_INPUTS,
    CONSUMPTION_TASK,
    ALIGN_OUTPUTS,
    STREAM_OUTPUTS,
    NUM_PROGRAMS
};

void printMatrix(std::string matrix_name, std::vector<float> matrix, int cols) {
  std::cout << matrix_name << std::endl;

  for (int i = 0; i < matrix.size(); i++) {

    std::cout << std::fixed << matrix[i] << "\t";
    
    if ( (i+1)%cols == 0) {
      std::cout << std::endl;
    }

  }

  std::cout << std::endl;

}

// void frontEnd_TensorDecomp(bool& flag, long unsigned int& rows, long unsigned int& cols, long unsigned int& exp_size, std::vector<float>& cpu_input0, std::vector<float>& cpu_output0, std::vector<float>& cpu_output1) {
//     /* Create data to input into back-end */
//     std::random_device rd;
//     std::mt19937 gen(rd());
//     std::uniform_real_distribution<float> distribution(0.0f, 100.0f);

//     for (int i = 0; i < rows; i++) {
//         for (int j = 0; j < cols; j++) {
//             cpu_input0[j+(cols*i)] = distribution(gen);
//         }
//     }
//     printMatrix("GenMatrix", cpu_input0, cols);

//     flag = true;
//     /* Loop to create multiple matrices and decompose */
//     for (int i = 0; i < exp_size; i++) {
        
//         for (int i = 0; i < rows; i++) {
//             for (int j = 0; j < cols; j++) {
//                 cpu_input0[j+(cols*i)] = distribution(gen);
//             }
//         }

//         while(flag) {}
//         printMatrix("QMatrix", cpu_output0, cols);
//         printMatrix("RMatrix", cpu_output1, cols);
//         sleep(1);
//     }
// }

// void backEnd_TensorDecomp(poplar::Engine& engine, bool& flag, long unsigned int& exp_size) {
//     for (int i = 0; i < exp_size; i++) {
//         while(!flag) {}
//         flag = false;
//         engine.run(Progs::STREAM_INPUTS);
//         engine.run(Progs::ALIGN_INPUTS);
//         engine.run(Progs::CONSUMPTION_TASK);
//         engine.run(Progs::ALIGN_OUTPUTS);
//         engine.run(Progs::STREAM_OUTPUTS);
//     }
// }

void tensorDecomp() {
    std::cout << "Getting Device..." << std::endl;
    // Get an IPU Device
    auto manager = poplar::DeviceManager::createDeviceManager();
    auto hwDevices = manager.getDevices(poplar::TargetType::IPU, 1);
    auto it = std::find_if(hwDevices.begin(), hwDevices.end(), [](poplar::Device &device) { return device.attach(); });
    poplar::Device device;

    if (it != hwDevices.end()) {
        device = std::move(*it);
    }

    std::cout << "Got Device!" << std::endl;

    /* Expose Shared Memory */

    // Graph
    std::cout << "Creating Graph..." << std::endl;
    poplar::Graph graph(device.getTarget());
    std::cout << "Created Graph!" << std::endl;

    // Programs
    std::vector<poplar::program::Program> progs(Progs::NUM_PROGRAMS);

    // Flags
    bool flag = false;

    // Parameters
    long unsigned int rows = 3;
    long unsigned int cols = 3;
    long unsigned int packet_size = 9;
    long unsigned int num_transfers = (rows*cols) /packet_size;
    long unsigned int exp_size = 1;

    // Tensors
    std::cout << "Adding Tensors..." << std::endl;
    auto input_tensor0 = graph.addVariable(poplar::FLOAT, {packet_size}, "Input Tensor 0");
    auto consumption_tensor_in0 = graph.addVariable(poplar::FLOAT, {rows*cols}, "Consumption Task Input 0");
    auto consumption_tensor_in0_exp = graph.addVariable(poplar::FLOAT, {rows, cols}, "Consumption Task Input 0 Expanded");
    auto consumption_tensor_out0 = graph.addVariable(poplar::FLOAT, {rows, cols}, "Consumption Task Output 0");
    auto consumption_tensor_out0_flat = graph.addVariable(poplar::FLOAT, {rows*cols}, "Consumption Task Output 0 Flattened");
    auto consumption_tensor_out1 = graph.addVariable(poplar::FLOAT, {rows, cols}, "Consumption Task Output 1");
    auto consumption_tensor_out1_flat = graph.addVariable(poplar::FLOAT, {rows*cols}, "Consumption Task Output 1 Flattened");
    auto output_tensor0 = graph.addVariable(poplar::FLOAT, {packet_size}, "Output Tensor 0");
    auto output_tensor1 = graph.addVariable(poplar::FLOAT, {packet_size}, "Output Tensor 1");

    auto identity_tensor = graph.addVariable(poplar::FLOAT, {rows, cols}, "Identity Tensor");

    std::cout << "Added Tensors!" << std::endl;

    std::cout << "Mapping Tensors..." << std::endl;
    poputil::mapTensorLinearly(graph, input_tensor0);
    poputil::mapTensorLinearly(graph, consumption_tensor_in0);
    poputil::mapTensorLinearly(graph, consumption_tensor_in0_exp);
    poputil::mapTensorLinearly(graph, consumption_tensor_out0);
    poputil::mapTensorLinearly(graph, consumption_tensor_out0_flat);
    poputil::mapTensorLinearly(graph, consumption_tensor_out1);
    poputil::mapTensorLinearly(graph, consumption_tensor_out1_flat);
    poputil::mapTensorLinearly(graph, output_tensor0);
    poputil::mapTensorLinearly(graph, output_tensor1);

    poputil::mapTensorLinearly(graph, identity_tensor);

    std::cout << "Mapped Tensors!" << std::endl;

    std::cout << "Adding Codelets..." << std::endl;
    // Add standard codelets
    popops::addCodelets(graph);

    // Add custom codelets
    graph.addCodelets("./device_libraries/io_codelet.gp");

    std::cout << "Added Codelets!" << std::endl;

    std::cout << "Adding Vertices..." << std::endl;
    // Vertices
    auto consumption_task_cs = graph.addComputeSet("Consumption Task CS");
    auto input_io0 = graph.addVertex(consumption_task_cs, "IOVertex");
    auto output_io0 = graph.addVertex(consumption_task_cs, "IOVertex");
    auto output_io1 = graph.addVertex(consumption_task_cs, "IOVertex");

    std::cout << "Added Vertices!" << std::endl;

    std::cout << "Mapping Vertices..." << std::endl;
    graph.setTileMapping(input_io0, 3);
    graph.setTileMapping(output_io0, 4);
    graph.setTileMapping(output_io1, 5);

    std::cout << "Mapped Vertices!" << std::endl;

    std::cout << "Adding Streams..." << std::endl;
    // Streams
    auto input_strm0 = graph.addHostToDeviceFIFO("Input Stream 0", poplar::FLOAT, packet_size);
    auto output_strm0 = graph.addDeviceToHostFIFO("Output Stream 0", poplar::FLOAT, packet_size);
    auto output_strm1 = graph.addDeviceToHostFIFO("Output Stream 1", poplar::FLOAT, packet_size);

    std::cout << "Added Streams!" << std::endl;

    // Misc
    //auto ready_flag = graph.addVariable(poplar::INT, {1}, "Ready Flag");
    //auto num_elements = graph.addVariable(poplar::INT, {1}, "Number of elements");
    std::vector<std::size_t> dimShape = {rows, cols};
    std::vector<std::size_t> flatShape = {rows*cols};

    //poputil::mapTensorLinearly(graph, ready_flag);
    //poputil::mapTensorLinearly(graph, num_elements);

    // CPU Vectors
    std::vector<float> cpu_input0(rows*cols);
    std::vector<float> cpu_output0(rows*cols);
    std::vector<float> cpu_output1(rows*cols);

    /* Stream Inputs Program */

    auto seq = poplar::program::Sequence();

    for(int i = 0; i < num_transfers; i++) {
        seq.add(poplar::program::Copy(input_strm0, input_tensor0));
    }

    progs[Progs::STREAM_INPUTS] = seq;

    graph.connect(input_io0["strm_in"], input_tensor0);
    graph.connect(input_io0["strm_out"], consumption_tensor_in0);

    /* Align Consumption Inputs Program */

    seq = poplar::program::Sequence();

    seq.add(poplar::program::Copy(consumption_tensor_in0_exp, consumption_tensor_in0.reshape(dimShape)));
    graph.setTileMapping(consumption_tensor_in0_exp, 3);

    progs[Progs::ALIGN_INPUTS] = seq;

    /* Consumption Task Program */

    seq = poplar::program::Sequence();

    poplin::addCodelets(graph);

    poplin::experimental::QRFactorization(graph, consumption_tensor_in0_exp, consumption_tensor_out0, seq);

    progs[Progs::CONSUMPTION_TASK] = seq;

    /* Align Consumption Outputs Program */

    seq = poplar::program::Sequence();

    seq.add(poplar::program::Copy(consumption_tensor_out0.reshape(flatShape), consumption_tensor_out0_flat));
    graph.setTileMapping(consumption_tensor_out0_flat, 4);

    seq.add(poplar::program::Copy(consumption_tensor_out1.reshape(flatShape), consumption_tensor_out1_flat));
    graph.setTileMapping(consumption_tensor_out0_flat, 5);

    progs[Progs::ALIGN_OUTPUTS] = seq;

    /* Stream Outputs Program */

    seq = poplar::program::Sequence();

    progs[Progs::STREAM_OUTPUTS] = seq;

    for(int i = 0; i < num_transfers; i++) {
        seq.add(poplar::program::Copy(output_tensor0, output_strm0));
        seq.add(poplar::program::Copy(output_tensor1, output_strm1));
    }

    graph.connect(output_io0["strm_in"], consumption_tensor_out0_flat);
    graph.connect(output_io0["strm_out"], output_tensor0);

    graph.connect(output_io1["strm_in"], consumption_tensor_out1_flat);
    graph.connect(output_io1["strm_out"], output_tensor1);

    auto exe = poplar::compileGraph(graph, progs);
    poplar::Engine engine(std::move(exe));
    engine.load(device);

    std::cout << "Loaded Device" << std::endl;

    #pragma omp parallel sections
    {
        #pragma omp section
        {
            /* Create data to input into back-end */
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_real_distribution<float> distribution(0.0f, 100.0f);

            for (int i = 0; i < rows; i++) {
                for (int j = 0; j < cols; j++) {
                    cpu_input0[j+(cols*i)] = distribution(gen);
                }
            }
            printMatrix("GenMatrix", cpu_input0, cols);

            flag = true;
            /* Loop to create multiple matrices and decompose */
            for (int i = 0; i < exp_size; i++) {
        
                for (int i = 0; i < rows; i++) {
                    for (int j = 0; j < cols; j++) {
                        cpu_input0[j+(cols*i)] = distribution(gen);
                    }
                }

            while(flag) {}
            printMatrix("QMatrix", cpu_output0, cols);
            printMatrix("RMatrix", cpu_output1, cols);
            sleep(1);
            }
        }

        #pragma omp section
        {
            for (int i = 0; i < exp_size; i++) {
                while(!flag) {}
                flag = false;
                //engine.run(Progs::STREAM_INPUTS);
                //engine.run(Progs::ALIGN_INPUTS);
                //engine.run(Progs::CONSUMPTION_TASK);
                //engine.run(Progs::ALIGN_OUTPUTS);
                //engine.run(Progs::STREAM_OUTPUTS);
            }
        }
    }
    return;
}