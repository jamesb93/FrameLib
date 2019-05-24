import json
import xml.etree.ElementTree as et
import os
from strippers import strip_space

class jParseAndBuild():
    '''
    Performs a comprehensive parse of the .maxref files to interact with the umenu in real-time.
    The information is stored in a dict.
    '''

    def __init__(self):
        self.tree         = 0
        self.root         = 0
        self.j_master_dict = dict({})

    def extract_from_refpage(self, x):
        self.tree = et.parse(x)
        self.root = self.tree.getroot() # c74object
        blank_param_dict = {}
        blank_internal = {}
        blank_descr = ''
        enums = ''

        # Find Information #    
        self.object_name = self.root.get('name') # get the object name
        param_idx = 0 # reset a variable to track the parameter number
        for child in self.root: # iterate over the sections
            if child.tag == 'misc': # if the section is misc
                if child.get('name') == 'Parameters': # if the section is misc and has name='Parameters'
                    for elem in child: # for sub-sections
                        blank_internal = {'name' : elem.get('name')} # store the name with the key/pair 'name'

                        for description in elem: # get the description out
                            blank_desc = strip_space(description.text)
                            
                            for bullet in description: # if there are any bullet points
                                if bullet.text != None: # and its not none
                                    if bullet.text[1] == '0':
                                        blank_desc += f'\n\nParameter Options:'

                                    blank_desc += f'\n{bullet.text}'
                        blank_internal['description'] = blank_desc # set the description
                        blank_param_dict[param_idx] = blank_internal # assign the blank_internal dict to a parameter number
                        param_idx += 1

        param_dict = dict({self.object_name:blank_param_dict})

        self.j_master_dict.update(param_dict)
        
        # once we've made the param stuff we append it to a dict with the name of the object
        # 

# ----------- THE GUTS ----------- #

def main(root):
    '''
    Creates a dict for the Max Documentation system.
    This dict contains more detailed information displayed in real-time when hovering over a certain tutorial in the umenu.

    Args:
        arg1: passes the root of the python files from the master script. Creates relative directories.
    '''
    # paths are determined relatively.
    dir_path = root
    dir_path = dir_path.replace('/Documentation/Max Documentation', '/Current Test Version/FrameLib')
    ref_dir = f'{dir_path}/docs/refpages' 
    obj_lookup = f'{dir_path}/interfaces/FrameLib-obj-jlookup.json'

    worker = jParseAndBuild() # make an instance of the class

    for filename in os.listdir(ref_dir):
        if filename != '.DS_Store':
            if filename != '_c74_ref_modules.xml':
                current_category = filename
                source_file_name = f'{ref_dir}/{filename}'

        for filename in os.listdir(source_file_name):
            if filename != '.DS_Store':
                source_file = f'{ref_dir}/{current_category}/{filename}'    
                worker.extract_from_refpage(source_file)

    with open(obj_lookup, 'w') as fp:
        json.dump(worker.j_master_dict, fp, indent=4)










        








