from urllib.parse import urlparse
from termcolor import colored
import tomllib
import tomli_w
import pprint
import signal
import sys


metadata=dict()

def write_toml():
    output_file_name = "metadata.toml"
    with open(output_file_name, "wb") as toml_file:
        tomli_w.dump(metadata, toml_file)
    
def signal_handler(sig, frame):
    print('\nCtrl+C pressed! Exiting gracefully.')
    write_toml()
    sys.exit(0)
    
def is_url(url_string):
    try:
        result = urlparse(url_string)
        # Check if both scheme and network location are present and non-empty
        return all([result.scheme, result.netloc])
    except ValueError: # Handle potential parsing errors, though urlparse usually handles them gracefully
        return False

def get_url(prompt, label):
    keep=existing_or_new("Please input the URL "+prompt+" [URL, required] ", label)
    if keep == "KEEP":
        return metadata[label]

    while True:
        data=keep
        if (not is_url(data)):
            print("This is not a URL. Please input a valid URL.")
            keep=input()
        else:
            break
    return data

def get_nonblank(prompt, label):
    keep=existing_or_new(prompt+" [text or URL, required] ", label)
    if keep == "KEEP":
        return metadata[label]
    while True:
        data=keep
        if data.strip() == "":            
            keep=input("This response cannot be blank.")
        else:
            break
    return data

def get_blank(prompt, label):
    keep=existing_or_new(prompt+" [text or URL, optional] ", label)
    if keep == "KEEP":
        return metadata[label]
    else:
        return keep

def get_nonblank_multi(prompt, label):
    keep=existing_or_new(prompt+" [text or URL, required, end-w-newline] ", label)
    if keep == "KEEP":
        return metadata[label]
    
    while True:
        odata=""
        data=keep
        while True:
            if data.strip() == "" and odata != "":
                break
            else:
                if odata != "":
                    odata += "\n"
                odata += data
                data=input()
        if odata.strip() == "":
            print("This response cannot be blank.")
        else:
            break
    return odata

def get_blank_multi(prompt, label):
    keep=existing_or_new(prompt+" [text or URL, optional, end-w-newline] ", label)
    if keep == "KEEP":
        return metadata[label]

    odata=""
    data=keep
    while True:
        if data.strip() == "":
            break
        else:
            if odata != "":
                odata += "\n"
            odata += data
            data=input()
    return odata

def existing_or_new(prompt, label):
    global metadata
    print(colored('\n' + prompt, 'blue'))
    if label in metadata:
        print("You have already responded", colored(metadata[label], 'red'), "to this question. Press [ENTER] to keep your response or provide a new response")
        newi=input()
        if newi.strip() == "":
            return "KEEP"
        else:
            return newi.strip()
    else:
        newi=input()
        return newi.strip()

def get_code_artifact(badge):
    if badge == 'f' or badge == 'r':
        pubpriv=input(colored("\nDoes your artifact use public infrastructure? [y]es or [n]o ", 'blue'))

        if pubpriv != "y" and pubpriv != "Y":
            pubpriv="n"
        else:
            pubpriv="y"

        if pubpriv=="y":
            metadata['infrastructure_url'] = get_url("the infrastructure you used", 'infrastructure_url')
            metadata['infrastructure_resources']=get_url("the file that describes infrastructure resources you used.", 'infrastructure_resources')
            metadata['infrastructure_allocation']=get_blank("Please input any special allocation instructions.",'infrastructure_allocation')
        else:
            metadata['infrastructure_constraints']=get_nonblank("Please input the reason for not using public infrastructure, such as special hardware that was not available.", 'infrastructure_constraints')
            metadata['infrastructure_access']=get_blank("Please input instructions for how evaluators can access your private infrastructure. If you leave this blank, evaluators will try to evaluate on their private infrastructure, but they may run into issues.", 'infrastructure_access')

        metadata['install_script']=get_url("your installation script.", 'install_script')
        metadata['use']=get_blank("Please provide any notes about intended use of your artifact and any limitations of your research artifact, i.e., what use is it NOT suitable for.", 'use')
        metadata['destructive']=get_blank("Please provide any notes about destructive actions your artifact can cause.", 'destructive')
        num=1
        # Ask about how many claims
        while True:
            metadata['claim'+str(num)]=get_nonblank("Please input text of claim "+str(num), 'claim'+str(num))
            metadata['script'+str(num)]=get_url("the claim-running script or instructions.", 'script'+str(num))
            metadata['expected'+str(num)]=get_nonblank("Describe the expected outcome that would validate the claim. This can also be a pointer to a file in your repository", 'expected'+str(num))
            while True:
                more=input(colored("\nDo you have more claims? [y]es or [n]o ", 'blue'))
                more = more.lower().strip()
                if more != "y" and more != "n":
                    print("Valid respones only include [y] or [n]")
                else:
                    break
            if more == "n":
                break
            num += 1
            
    metadata['hw']=get_blank_multi("Please specify any special hw requirements for evaluators, such as CPU, GPU, memory, etc. ", 'hw')
    metadata['sw']=get_blank_multi("Please specify any special sw requirements for evaluators, such as OS, paid software packages or software that requires a non-Linux environment to run ", 'sw')
    metadata['api']=get_blank_multi("Please specify if your artifact needs API keys to access paid services online.", 'api')
    keep=existing_or_new("Does your artifact require a GUI - [y]es or [n]o ", 'gui')
    if keep != "KEEP":
        gui = keep.lower().strip()
        while True:
            if gui != "y" and gui != "n":
                print("Valid respones only include [y] or [n]")
                gui=input().lower().strip()
            else:
                break
        metadata['gui'] = gui

    return


def get_data_artifact():
    
    survey=input(colored("\nDoes your artifact include only user survey instruments (without results)? [y]es or [n]o ", 'blue'))

    if survey!="y" and survey !="Y":
        survey="n"
    else:
        survey="y"

    if survey == "n":
        metadata['provenance']=get_url("the file (ideally in your artifact repository) that describes how data was collected, where, when the collection started and ended. Be as detailed as possible.", 'provenance')
        metadata['use']=get_blank("Please provide any notes about intended use of your artifact and any limitations of your data collection.", 'use')
        metadata['ethics']=get_url("the file that discusses ethics of your data collection process. This can be a copy of the ethics section from your paper.", 'ethics')
        
    return

if __name__ == "__main__":
    # Register the signal handler for SIGINT (Ctrl+C)
    signal.signal(signal.SIGINT, signal_handler)
    
    print("Please respond to the following questions. If you press ctrl-C your results will be saved and restored on the next run.")
    try:
        with open('metadata.toml', 'r') as file:
            content = file.read()
            metadata = tomllib.loads(content)
            #print("Parsed Dictionary:")
            #pprint.pprint(metadata)
    except Exception as e:
        pass
            
    keep = existing_or_new("Please specify which badge you are requesting - [a]vailable, [f]unctional or [r]esults reproduced ", 'badge')
    if keep != "KEEP":
        while True:
            badge = keep.lower().strip()
            if badge != "a" and badge != "f" and badge != "r":
                print("Valid responses only include [a], [f] or [r]")
                keep=input().lower().strip()
            else:
                break
            
        metadata['badge'] = badge
            
    metadata['artifact_url']=get_url("your artifact, e.g., Github repo ", 'artifact_url')


    keep = existing_or_new("Is your artifact [c]ode, [d]ataset or [b]oth? ", 'cd')
    if keep != "KEEP":
        cd = keep.lower().strip()
        while True:
            if cd != "c" and cd != "d" and cd != "b":
                print("Valid respones only include [c], [d] or [b]")
                cd=input().lower().strip()
            else:
                break

        metadata['cd'] = cd

    metadata['citation']=get_nonblank_multi("Please input your paper's citation in bibtex format. It is OK if this is an incomplete citation, e.g., missing a DOI or page numbers. ", 'citation')
    metadata['license_url']=get_blank_multi("Please provide a URL for the license you plan to use to release the artifact to the public. Leave blank if none. ", 'license_url')

    if (metadata['cd'] == "c" or metadata['cd'] == "b") and metadata['badge'] != 'a':
        get_code_artifact(metadata['badge'])

    if metadata['cd'] == "d" or metadata['cd'] == "b":
        get_data_artifact()

    metadata['readme']=get_url("the README file that contains any remaining instructions about your artifact, either to the AEC or for future reuse.", 'readme')

    print("\n\nPlease submit the contents of metadata.toml file to HotCRP ===")

    write_toml()
