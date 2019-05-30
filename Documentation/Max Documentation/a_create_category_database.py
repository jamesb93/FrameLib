import os
import json
from strippers import strip_extension

def main(root):
    '''
    This creates a category database in .json format.
    This is used by edit_raw_XML.py to assign object categories to the xml files.
    '''
    dir_path = root
    object_path = dir_path.replace('/Documentation/Max Documentation', '/FrameLib_Max_Objects')
    output_path = f'{dir_path}/category_database.json'

    file_categories = (os.listdir(object_path))
    
    try:
        file_categories.remove('_MaxSDK_')
    except ValueError:
        print('No _MaxSDK_ to delete')
        pass
    
    try:
        file_categories.remove('.DS_Store')
    except ValueError:
        print('No .DS_Store')
        pass

    try:    
        file_categories.remove('Common')
    except ValueError:
        print('No common folder')
        pass

    file_categories.sort()

    category_dict = dict({})

    for item in file_categories:
        files = os.listdir(f'{object_path}/{item}')
        if 'ibuffer' in files:
            files.remove('ibuffer')
        # some max categories already overlap with framelib categories (timing for example). This just maps Timing -> fl_timing to avoid any duplication issues
        item = f'fl_{item}'
        for i in range(len(files)):
            files[i] = strip_extension(files[i], 1)
        category_dict[item] = files
        
    with open(output_path, 'w+') as fp:
        json.dump(category_dict, fp, indent=4)