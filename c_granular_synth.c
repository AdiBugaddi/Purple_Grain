/**
 * @file c_granular_synth.c
 * @author Kretschmar, Nikita 
 * @author Philipp, Adrian 
 * @author Strobl, Micha 
 * @author Wennemann,Tim <br>
 * Audiocommunication Group, Technische Universität Berlin <br>
 * @brief main file of the synthesizer's implementation
 * @version 0.1
 * @date 2021-07-25
 *
 * @copyright Copyright (c) 2021
 *
 */

#include "c_granular_synth.h"
#include "envelope.h"
#include "grain.h"
#include "purple_utils.h"

/**
 * @brief initial setup of soundfile and adjustment silder related variables
 * @details 
 * @param soundfile contains the soundfile which can be read in via inlet <br>
 * @param soundfile_length lenght of the soundfile in samples <br>
 * @param grain_size_ms size of a grain in milliseconds, adjustable through slider <br>
 * @param start_pos position within the soundfile, adjustable through slider <br>
 * @param time_stretch_factor resizes sample length within a grain, for negative values read samples in backwards direction, adjustable through slider <br>
 * @param attack attack time in the range of 0 - 4000ms, adjustable through slider <br>
 * @param decay decay time in the range of 0 - 4000ms, adjustable through slider <br>
 * @param sustain sustain time in the range of 0 - 1, adjustable through slider <br>
 * @param release release time in the range of 0 - 10000ms, adjustable through slider <br>
 * @return c_granular_synth* 
 */
c_granular_synth *c_granular_synth_new(t_word *soundfile, int soundfile_length, int grain_size_ms, int start_pos, float time_stretch_factor, int attack, int decay, float sustain, int release, float gauss_q_factor)
{
    c_granular_synth *x = (c_granular_synth *)malloc(sizeof(c_granular_synth));
    x->soundfile_length = soundfile_length;
    x->sr = sys_getsr();
    x->grain_size_ms = grain_size_ms;
    x->grain_size_samples = get_samples_from_ms(x->grain_size_ms, x->sr);
    x->soundfile_table = (float *) malloc(x->soundfile_length * sizeof(float));
    x->time_stretch_factor = time_stretch_factor;
    x->reverse_playback = (x->time_stretch_factor < 0);
    x->output_buffer = 0.0;
    x->current_start_pos = start_pos;
    x->current_grain_index = 0;
    x->current_gauss_stage_index = 0;
    c_granular_synth_adjust_current_grain_index(x);
    
    c_granular_synth_reset_playback_position(x);
    
    x->current_adsr_stage_index = 0;
    x->adsr_env = envelope_new(attack, decay, sustain, release);
    
    // Retrigger when user sets different grain size
    c_granular_synth_set_num_grains(x);
    post("C main file - new method - number of grains = %d", x->num_grains);
    c_granular_synth_adjust_current_grain_index(x);
    
    for(int i = 0; i<soundfile_length;i++)
    {
        x->soundfile_table[i] = soundfile[i].w_float;
    }
    
    x->grains_table = NULL;
    c_granular_synth_populate_grain_table(x);

    return x;
}

/**
 * @brief refresh plaback positions, opens grain scheduleing, writes gaus value, writes into output
 * 
 * @param x input pointer of c_granular_synth_process object
 * @param in input
 * @param out output
 * @param vector_size vectoral size of 
 */
void c_granular_synth_process(c_granular_synth *x, float *in, float *out, int vector_size)
{
    int i = vector_size;
    float gauss_val, adsr_val;
    
    while(i--)
    {
        x->output_buffer = 0;
        // oder kann man playback jetzt einfach immer +1 hochgehen?
        x->playback_position++;
        if(x->playback_position >= x->soundfile_length)
        {
            x->playback_position = 0;
        }
        if(x->playback_position >= x->playback_cycle_end)
        {
            x->playback_position = x->current_start_pos;
        }
        //ab hier dann schauen welches grain aktiv ist
        //x->playback_position = x->grains_table[x->current_grain_index].current_sample_pos;
        
        grain_internal_scheduling(&x->grains_table[x->current_grain_index], x);
        
        //gauss_val = gauss(x->gauss_q_factor, x->grains_table[x->current_grain_index],x->grains_table[x->current_grain_index].end - x->playback_position);
        gauss_val = gauss(x);
        //gauss_val = gauss(x->gauss_q_factor, x->grain_size_samples,x->current_grain_index);
        x->output_buffer *= gauss_val;
        
        
        if(x->midi_velo > 0)
        {
            adsr_val = calculate_adsr_value(x);
        }
        else
        {
            if(x->adsr_env->adsr == SILENT)
            {
                adsr_val = 0;
            }
            // Must be in Release State
            else
            {
                if(x->adsr_env->adsr != RELEASE) x->current_adsr_stage_index = 0;
                x->adsr_env->adsr = RELEASE;
                //x->current_adsr_stage_index = 0;
                adsr_val = calculate_adsr_value(x);
            }
        }
        x->output_buffer *= adsr_val;
        
        
        
        *out++ = x->output_buffer;
    }
    
}

/**
 * @brief sets number of grains
 * sets number of grains according to @a soundfile_length and @a grain_size_samples <br>
 * @param x input pointer of @a c_granular_synth_set_num_grains object
 */
void c_granular_synth_set_num_grains(c_granular_synth *x)
{
    x->num_grains = (int)ceilf(fabsf(x->soundfile_length * x->time_stretch_factor) / x->grain_size_samples);
}
/**
 * @brief adjusts current grain index
 * adjusts current grain index according to @a currents_start_pos and @a grain_size_samples
 * @param x input pointer of @a c_granular_synth_adjust_current_grain_index object
 */
void c_granular_synth_adjust_current_grain_index(c_granular_synth *x)
{
    //int index = x->current_start_pos / x->grain_size_samples;
    int index = ceil((x->current_start_pos * fabs(x->time_stretch_factor)) / x->grain_size_samples);
    x->current_grain_index = index;
}
/**
 * @brief generates a grain table
 * generates a grain table according to @a current_grain_index
 * for negative @a time_stretch_factor values samples are read in backwards direction
 * @param x input pointer of @a c_granular_synth_populate_grain_table object
 */
void c_granular_synth_populate_grain_table(c_granular_synth *x)
{
    grain *grains_table;
    grains_table = (grain *) calloc(x->num_grains, sizeof(grain));
    int j;
    float start_offset = 0;
    // Grain Table schreiben ab "current_grain_index"
    // Bis jetzt schreibt for schlaife nur bis ans Ende der Num Grains
    // Muss als Ring Buffer auch die ersten Grains befüllen!!
    
    
    
    if(x->reverse_playback)
    {
        for(j = x->current_grain_index; j >= 0; j--)
        {
            grains_table[j] = grain_new(x->grain_size_samples,
                                        x->soundfile_length,
                                        (x->current_start_pos + x->grain_size_samples), // ???
                                        j, x->time_stretch_factor);
            if(j < x->current_grain_index) grains_table[j+1].next_grain = &grains_table[j];
            /*
            if(grains_table[j].start >= x->playback_position &&  grains_table[j].end <= x->playback_position)
            {
                grains_table[j].grain_active = true;
            }
             */
            start_offset += x->time_stretch_factor * x->grain_size_samples;
        }
        grains_table[0].next_grain = &grains_table[x->num_grains - 1];
    }
    // Playback inf forward direction
    else
    {
        for(j = x->current_grain_index; j<x->num_grains; j++)
        {
            grains_table[j] = grain_new(x->grain_size_samples,
                                        x->soundfile_length,
                                        x->current_start_pos + (start_offset), // ???
                                        j, x->time_stretch_factor);
            if(j > 0) grains_table[j-1].next_grain = &grains_table[j];
            /*
            if(grains_table[j].start <= x->playback_position &&  grains_table[j].end >= x->playback_position)
            {
                grains_table[j].grain_active = true;
            }
             */
            start_offset += x->time_stretch_factor * x->grain_size_samples;
        }
        grains_table[x->num_grains - 1].next_grain = &grains_table[0];
    }
    
    // Das stand vorher in der process methode
    //x->playback_position = x->current_start_pos;
    c_granular_synth_reset_playback_position(x);
    
    if(x->grains_table) free(x->grains_table);
    x->grains_table = grains_table;
}
/**
 * @brief checks on current input states
 * @details checks slider positions, MIDI input and ADSR state to update correspondent values <br>
 * @param[in] x input pointer of c_granular_synth_properties_update object <br>
 * @param[in] midi_velo MIDI input velocity value, usable through virtual or external MIDI device <br>
 * @param[in] midi_pitch MIDI input pitch/key value, usable through virtual or external MIDI device, also used for noteon detection <br>
 * @param[in] grain_size_ms size of a grain in milliseconds, adjustable through slider <br>
 * @param[in] start_pos position within the soundfile, adjustable through slider <br>
 * @param[in] time_stretch_factor resizes sample length within a grain, adjustable through slider <br>
 * @param[in] attack attack time in the range of 0 - 4000ms, adjustable through slider <br>
 * @param[in] decay decay time in the range of 0 - 4000ms, adjustable through slider <br>
 * @param[in] sustain sustain time in the range of 0 - 1, adjustable through slider <br>
 * @param[in] release release time in the range of 0 - 10000ms, adjustable through slider <br>
 * @param[in] gauss_q_factor envelope manipulation value in the range of 0.01 - 1, adjustable through slider <br>
 */
void c_granular_synth_properties_update(c_granular_synth *x, int grain_size_ms, int start_pos, float time_stretch_factor, int midi_velo, int midi_pitch, int attack, int decay, float sustain, int release, float gauss_q_factor)
{
    if(x->grain_size_ms != grain_size_ms || x->current_start_pos != start_pos || x->time_stretch_factor != time_stretch_factor || !x->grains_table)
    {
        if(x->grain_size_ms != grain_size_ms)
        {
            x->grain_size_ms = grain_size_ms;
            int grain_size_samples = get_samples_from_ms(grain_size_ms, x->sr);
            x->grain_size_samples = grain_size_samples;
        }
        if(x->current_start_pos != start_pos)
        {
            x->current_start_pos = start_pos;
        }
        if(x->time_stretch_factor != time_stretch_factor)
        {
            x->time_stretch_factor = time_stretch_factor;
        }
        c_granular_synth_set_num_grains(x);
        c_granular_synth_adjust_current_grain_index(x);
        c_granular_synth_populate_grain_table(x);
    }
    
    if(x->midi_pitch != midi_pitch)
    {
        x->midi_pitch = midi_pitch;
    }
    
    if(x->midi_velo != midi_velo)
    {
        x->midi_velo = midi_velo;
    }
    
    if (x->adsr_env->attack != attack || x->adsr_env->decay != decay || x->adsr_env->sustain != sustain || x->adsr_env->release != release)
    {
        if(x->adsr_env->attack != attack)
        {
            x->adsr_env->attack = attack;
        }
        if(x->adsr_env->decay != decay)
        {
            x->adsr_env->decay = decay;
        }
        if(x->adsr_env->sustain != sustain)
        {
            x->adsr_env->sustain = sustain;
        }
        if(x->adsr_env->release != release)
        {
            x->adsr_env->release = release;
        }
        x->adsr_env = envelope_new(attack, decay, sustain, release);
    }

    if(x->gauss_q_factor != gauss_q_factor)
    {
        x->gauss_q_factor = gauss_q_factor;
    }
}
/**
 * @related pd_granular_synth_tilde
 * @brief resets playback position
 * 
 * @param x input pointer of @a c_granular_synth_reset_playback_position object
 */
void c_granular_synth_reset_playback_position(c_granular_synth *x)
{
    x->playback_position = x->current_start_pos;
    x->playback_cycle_end = x->current_start_pos + x->grain_size_samples;
}

/**
 * @related pd_granular_synth_tilde
 * @brief frees granular_synth object
 * 
 * @param x input pointer of @a c_granular_synth_free object
 */
void c_granular_synth_free(c_granular_synth *x)
{
    if(x)
    {
        free(x->soundfile_table);
        free(x->grains_table);
        envelope_free(x->adsr_env);
        free(x);
    }
}
