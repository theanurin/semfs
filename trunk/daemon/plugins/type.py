import mimetypes

def guess(fileName):
    """TODO:
    INPUT:
    OUTPUT:
    """
    type, encoding = mimetypes.guess_type(fileName)
    if type == 'audio/mpeg':
       return 'mp3'
    else:
	   return False 
    
