#include "../shared/parameters.h"
#include "../shared/c2dvector.h"
#include "../shared/particle.h"
#include "../shared/cell.h"
#include "box.h"

inline void timing_information(Node* node, clock_t start_time, int i_step, int total_step)
{
	if (node->node_id == 0)
	{
		clock_t current_time = clock();
		int lapsed_time = (current_time - start_time) / (CLOCKS_PER_SEC);
		int remaining_time = (lapsed_time*(total_step - i_step)) / (i_step + 1);
		cout << "\r" << round(100.0*i_step / total_step) << "% lapsed time: " << lapsed_time << " s		remaining time: " << remaining_time << " s" << flush;
	}
	MPI_Barrier(MPI_COMM_WORLD);
}


inline Real equilibrium(Box* box, long int equilibrium_step, int saving_period, ofstream& out_file)
{
	clock_t start_time, end_time;
	start_time = clock();

	if (box->thisnode->node_id == 0)
		cout << "equilibrium:" << endl;

	for (long int i = 0; i < equilibrium_step; i+=cell_update_period)
	{
		box->Multi_Step(cell_update_period);
//		timing_information(box->thisnode,start_time,i,equilibrium_step);
	}

	if (box->thisnode->node_id == 0)
		cout << "Finished" << endl;

	end_time = clock();

	Real t = (Real) (end_time - start_time) / CLOCKS_PER_SEC;
	return(t);
}


inline Real data_gathering(Box* box, long int total_step, int saving_period, ofstream& out_file)
{
	clock_t start_time, end_time;
	start_time = clock();

	#ifdef TRACK_PARTICLE
	if (!flag)
		flag = true;
	#endif

	if (box->thisnode->node_id == 0)
		cout << "gathering data:" << endl;
	int saving_time = 0;

	for (long int i = 0; i < total_step; i+=cell_update_period)
	{
		box->Multi_Step(cell_update_period);
//		timing_information(box->thisnode,start_time,i,total_step);
		if ((i / cell_update_period) % saving_period == 0)
			out_file << box;
	}

	if (box->thisnode->node_id == 0)
		cout << "Finished" << endl;

	end_time = clock();

	Real t = (Real) (end_time - start_time) / CLOCKS_PER_SEC;
	return(t);
}

void Change_Noise(int argc, char *argv[], Node* thisnode)
{
	Real input_rho = atof(argv[1]);
	Real input_g = atof(argv[2]);
	Real input_alpha = atof(argv[3]);

	vector<Real> noise_list;
	for (int i = 4; i < argc; i++)
		noise_list.push_back(atof(argv[i]));

	Real t_eq,t_sim;

	Particle::noise_amplitude = 0;
	Particle::g = input_g;
	Particle::alpha = input_alpha;

	Box box;
	box.Init(thisnode, input_rho);

	ofstream out_file;

	for (int i = 0; i < noise_list.size(); i++)
	{
		Particle::noise_amplitude = noise_list[i] / sqrt(dt); // noise amplitude depends on the step (dt) because of ito calculation. If we have epsilon in our differential equation and we descritise it with time steps dt, the noise in each step that we add is epsilon times sqrt(dt) if we factorise it with a dt we have dt*(epsilon/sqrt(dt)).
		box.info.str("");
		box.info << "rho=" << box.density <<  "-g=" << Particle::g << "-alpha=" << Particle::alpha << "-noise=" << noise_list[i] << "-cooling";

		if (thisnode->node_id == 0)
		{
			stringstream address;
			address.str("");
			address << box.info.str() << "-r-v.bin";
			out_file.open(address.str().c_str());
		}

		if (thisnode->node_id == 0)
			cout << " Box information is: " << box.info.str() << endl;

		MPI_Barrier(MPI_COMM_WORLD);
		t_eq = equilibrium(&box, equilibrium_step, saving_period, out_file);
		MPI_Barrier(MPI_COMM_WORLD);

		if (thisnode->node_id == 0)
			cout << " Done in " << (t_eq / 60.0) << " minutes" << endl;

		t_sim = data_gathering(&box, total_step, saving_period, out_file);
		MPI_Barrier(MPI_COMM_WORLD);

		if (thisnode->node_id == 0)
		{
			cout << " Done in " << (t_sim / 60.0) << " minutes" << endl;
			out_file.close();
		}
	}
	MPI_Barrier(MPI_COMM_WORLD);
}

void Change_Alpha(int argc, char *argv[], Node* thisnode)
{
	Real input_rho = atof(argv[1]);
	Real input_g = atof(argv[2]);
	Real input_noise = atof(argv[3]);

	vector<Real> alpha_list;
	for (int i = 4; i < argc; i++)
		alpha_list.push_back(atof(argv[i]));

	Real t_eq,t_sim;

	Particle::noise_amplitude = input_noise / sqrt(dt); // noise amplitude depends on the step (dt) because of ito calculation. If we have epsilon in our differential equation and we descritise it with time steps dt, the noise in each step that we add is epsilon times sqrt(dt) if we factorise it with a dt we have dt*(epsilon/sqrt(dt)).
	Particle::g = input_g;
	Particle::alpha = 0;

	Box box;
	box.Init(thisnode, input_rho);

	ofstream out_file;

	for (int i = 0; i < alpha_list.size(); i++)
	{
		Particle::alpha = alpha_list[i];
		box.info.str("");
		box.info << "rho=" << box.density <<  "-g=" << Particle::g << "-alpha=" << Particle::alpha << "-noise=" << input_noise << "-cooling";

		if (thisnode->node_id == 0)
		{
			stringstream address;
			address.str("");
			address << box.info.str() << "-r-v.bin";
			out_file.open(address.str().c_str());
		}

		if (thisnode->node_id == 0)
		{
			cout << " Box information is: " << box.info.str() << endl;
			Triangle_Lattice_Formation(box.particle, box.N, 1);
		}

		MPI_Barrier(MPI_COMM_WORLD);

		// Master node will broadcast the particles information
		thisnode->Root_Bcast();
		// Any node update cells, knowing particles and their cell that they are inside.
		thisnode->Full_Update_Cells();

		MPI_Barrier(MPI_COMM_WORLD);

		t_eq = equilibrium(&box, equilibrium_step, saving_period, out_file);
		MPI_Barrier(MPI_COMM_WORLD);

		if (thisnode->node_id == 0)
			cout << " Done in " << (t_eq / 60.0) << " minutes" << endl;

		t_sim = data_gathering(&box, total_step, saving_period, out_file);
		MPI_Barrier(MPI_COMM_WORLD);

		if (thisnode->node_id == 0)
		{
			cout << " Done in " << (t_sim / 60.0) << " minutes" << endl;
			out_file.close();
		}
	}
	MPI_Barrier(MPI_COMM_WORLD);
}

bool Run_From_File(int argc, char *argv[], Node* thisnode)
{
	Box box;

	#ifdef TRACK_PARTICLE
	track_p = &particle[track];
	#endif

	Real input_g;
	Real input_rho;
	Real input_noise;
	Real input_L;

	string name = input_name;
	stringstream address(name);
	ifstream is;
	is.open(address.str().c_str());
	if (!is.is_open())
		return false;

	boost::replace_all(name, "-r-v.bin", "");
	boost::replace_all(name, "rho=", "");
	boost::replace_all(name, "-g=", "");
	boost::replace_all(name, "-alpha=", "");
	boost::replace_all(name, "-noise=", "\t");
	boost::replace_all(name, "-L=", "\t");

	stringstream ss_name(name);
	ss_name >> density;
	ss_name >> input_g;
	ss_name >> input_alpha;
	ss_name >> input_noise;
	ss_name >> input_L;
	input_L = 60;
	if (input_L != Lx_int)
	{
		cout << "The specified box size " << input_L << " is not the same as the size in binary file which is " << Lx_int << " please recompile the code with the right Lx_int in parameters.h file." << endl;
		return false;
	}

	Particle::g = input_g;
	Particle::alpha = input_alpha;
	Particle::noise_amplitude = input_noise / sqrt(dt);

	thisnode = input_node;

	is.read((char*) &N, sizeof(int) / sizeof(char));
	if (N < 0 || N > 1000000)
		return (false);

	for (int i = 0; i < N; i++)
	{
		is >> particle[i].r;
		is >> particle[i].v;
	}

	is.close();

	MPI_Barrier(MPI_COMM_WORLD);

	Init_Topology(); // Adding walls

	if (thisnode->node_id == 0)
	{
		cout << "number_of_particles = " << N << endl; // Printing number of particles.
	}
	MPI_Barrier(MPI_COMM_WORLD);

// Master node will broadcast the particles information
	thisnode->Root_Bcast();
// Any node update cells, knowing particles and their cell that they are inside.
	thisnode->Full_Update_Cells();

	#ifdef verlet_list
	thisnode->Update_Neighbor_List();
	#endif

// Buliding up info stream. In next versions we will take this part out of box, making our libraries more abstract for any simulation of SPP.
	box.info.str("");
	box.info << "rho=" << box.density <<  "-k=" << Particle::kapa << "-mu+=" << Particle::mu_plus << "-mu-=" << Particle::mu_minus << "-Dphi=" << Particle::D_phi << "-L=" << Lx;

	ofstream out_file;
	if (thisnode->node_id == 0)
	{
		stringstream address;
		address.str("");
		address << "deviation-" << box.info.str() << ".dat";
		box.outfile.open(address.str().c_str());
	}

	if (thisnode->node_id == 0)
		cout << " Box information is: " << box.info.str() << endl;

	Real t_eq,t_sim;
	MPI_Barrier(MPI_COMM_WORLD);
 	t_sim = box.Lyapunov_Exponent(1, 10, 0.1, 10, 3);
	MPI_Barrier(MPI_COMM_WORLD);

	if (thisnode->node_id == 0)
	{
		cout << " Done in " << floor(t_sim / 60.0) << " minutes and " << t_sim - 60*floor(t_sim / 60.0) << " s" << endl;
		out_file.close();
	}
	
	MPI_Barrier(MPI_COMM_WORLD);
	return true;
}

void Init_Nodes(Node& thisnode)
{
	#ifdef COMPARE
		thisnode.seed = seed;
	#else
		thisnode.seed = time(NULL) + thisnode.node_id*112488;
		while (!thisnode.Chek_Seeds())
		{
			thisnode.seed = time(NULL) + thisnode.node_id*112488;
			MPI_Barrier(MPI_COMM_WORLD);
		}
	#endif
	C2DVector::Init_Rand(thisnode.seed);
	MPI_Barrier(MPI_COMM_WORLD);
}

int main(int argc, char *argv[])
{
	int this_node_id, total_nodes;
	MPI_Status status;
	MPI_Init(&argc, &argv);

	Node thisnode;
	Init_Nodes(thisnode);

	Change_Noise(argc, argv, &thisnode);
//	Change_Alpha(argc, argv, &thisnode);

	MPI_Barrier(MPI_COMM_WORLD);
	MPI_Finalize();
}

